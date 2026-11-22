#include "rfpch.h"
#include "ModelAnimation.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <numeric>
#include <fstream>

namespace Redflag
{
	namespace
	{
		struct NodePose
		{
			glm::vec3 Position, Scale;
			glm::quat Rotation;
		};

		struct Track
		{
			size_t Node;
			cgltf_animation_path_type Path;
			cgltf_interpolation_type Interpolation;
			std::vector<float> Times;
			std::vector<glm::vec4> Values;
		};

		struct Primitive
		{
			size_t Node;
			std::vector<size_t> Joints;
			std::vector<glm::mat4> InverseBind;
			std::unique_ptr<SkinnedMesh> Skin;
		};

		cgltf_result ReadFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data)
		{
			std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
			if (!input) return cgltf_result_file_not_found;
			const std::streamoff bytes = input.tellg();
			if (bytes < 0 || (*size && std::uintmax_t(bytes) < *size)) return cgltf_result_data_too_short;
			if (!*size) *size = static_cast<size_t>(bytes);
			*data = std::malloc(*size);
			if (!*data) return cgltf_result_out_of_memory;
			input.seekg(0);
			if (input.read(static_cast<char*>(*data), *size)) return cgltf_result_success;
			std::free(*data);
			*data = nullptr;
			return cgltf_result_io_error;
		}

		std::vector<float> Read(const cgltf_accessor* accessor)
		{
			if (!accessor) return {};
			std::vector<float> values(accessor->count * cgltf_num_components(accessor->type));
			if (cgltf_accessor_unpack_floats(accessor, values.data(), values.size()) != values.size())
				throw std::runtime_error("Cannot read glTF accessor");
			return values;
		}

		glm::quat Rotation(const glm::vec4& xyzw)
		{
			return glm::quat(xyzw.w, xyzw.x, xyzw.y, xyzw.z);
		}

		glm::vec4 SampleTrack(const Track& track, float time)
		{
			const bool cubic = track.Interpolation == cgltf_interpolation_type_cubic_spline;
			auto value = [&](size_t key) { return track.Values[cubic ? key * 3 + 1 : key]; };
			if (time <= track.Times.front()) return value(0);
			if (time >= track.Times.back()) return value(track.Times.size() - 1);

			size_t next = std::upper_bound(track.Times.begin(), track.Times.end(), time) - track.Times.begin();
			size_t prev = next - 1;
			float seconds = track.Times[next] - track.Times[prev];
			float t = (time - track.Times[prev]) / seconds;
			if (track.Interpolation == cgltf_interpolation_type_step) return value(prev);
			if (cubic)
			{
				// glTF 的切线给出每秒的变化量，乘两帧的时间差才能用于这段 Hermite 插值。
				float t2 = t*t, t3 = t2*t;
				return (2*t3-3*t2+1)*value(prev) + (t3-2*t2+t)*seconds*track.Values[3*prev+2]
					+ (-2*t3+3*t2)*value(next) + (t3-t2)*seconds*track.Values[3*next];
			}
			if (track.Path == cgltf_animation_path_type_rotation)
			{
				glm::quat q = glm::slerp(Rotation(value(prev)), Rotation(value(next)), t);
				return { q.x, q.y, q.z, q.w };
			}
			return glm::mix(value(prev), value(next), t);
		}

		std::unique_ptr<SkinnedMesh> ReadSkin(const cgltf_primitive& primitive, size_t jointCount)
		{
			auto attribute = [&](cgltf_attribute_type type, int set = 0) {
				return Read(cgltf_find_accessor(&primitive, type, set));
			};
			std::vector<float> positions = attribute(cgltf_attribute_type_position);
			std::vector<float> normals = attribute(cgltf_attribute_type_normal);
			std::vector<float> uv = attribute(cgltf_attribute_type_texcoord);
			std::vector<float> tangents = attribute(cgltf_attribute_type_tangent);
			std::vector<float> emissionUV;
			if (primitive.material && primitive.material->emissive_texture.texcoord)
				emissionUV = attribute(cgltf_attribute_type_texcoord, primitive.material->emissive_texture.texcoord);
			std::vector<MeshVertex> vertices(positions.size() / 3);
			for (size_t i = 0; i < vertices.size(); ++i)
			{
				MeshVertex& v = vertices[i];
				v.Position = glm::make_vec3(&positions[3*i]);
				v.Normal = normals.empty() ? glm::vec3(0) : glm::make_vec3(&normals[3*i]);
				v.Tangent = tangents.empty() ? glm::vec3(0) : glm::make_vec3(&tangents[4*i]);
				v.TexCoord = uv.empty() ? glm::vec2(0) : glm::vec2(uv[2*i], 1.f - uv[2*i+1]);
				if (!emissionUV.empty()) v.EmissiveUVOffset = glm::vec2(emissionUV[2*i], 1.f-emissionUV[2*i+1]) - v.TexCoord;
			}

			std::vector<uint32_t> indices(primitive.indices ? primitive.indices->count : vertices.size());
			if (primitive.indices) {
				if (cgltf_accessor_unpack_indices(primitive.indices, indices.data(), sizeof(uint32_t), indices.size()) != indices.size())
					throw std::runtime_error("Unsupported glTF index accessor");
			} else std::iota(indices.begin(), indices.end(), 0u);
			if (indices.size() % 3 != 0) throw std::runtime_error("Incomplete glTF triangle");
			std::vector<SkinWeights> weights(vertices.size());
			for (size_t a = 0; a < primitive.attributes_count; ++a)
			{
				const cgltf_attribute& attr = primitive.attributes[a];
				if (attr.type != cgltf_attribute_type_joints) continue;
				std::vector<float> joints = Read(attr.data);
				std::vector<float> amounts = attribute(cgltf_attribute_type_weights, attr.index);
				if (joints.size() != 4*vertices.size() || amounts.size() != joints.size())
					throw std::runtime_error("Unpaired glTF skin joints and weights");
				// 引擎每个顶点保留影响最大的四个关节；丢弃较小权重后重新归一。
				for (size_t k = 0; k < amounts.size(); ++k)
				{
					SkinWeights& skin = weights[k/4];
					auto least = std::min_element(skin.Weights.begin(), skin.Weights.end());
					if (amounts[k] <= *least) continue;
					skin.Joints[least - skin.Weights.begin()] = unsigned(joints[k]);
					*least = amounts[k];
				}
			}
			for (SkinWeights& skin : weights) {
				float sum = std::accumulate(skin.Weights.begin(), skin.Weights.end(), 0.f);
				if (!(sum > 0.f)) throw std::runtime_error("Vertex has no skin weights");
				for (float& weight : skin.Weights) weight /= sum;
			}
			// 没有法线时，按 glTF 约定每个三角形使用自己的面法线，棱角处不能共用顶点。
			if (normals.empty())
			{
				std::vector<MeshVertex> faces(indices.size());
				std::vector<SkinWeights> faceWeights(indices.size());
				for (size_t i = 0; i < indices.size(); ++i) {
					faces[i] = vertices.at(indices[i]);
					faceWeights[i] = weights.at(indices[i]);
				}
				for (size_t i = 0; i < faces.size(); i += 3) {
					glm::vec3 n = glm::cross(faces[i+1].Position-faces[i].Position, faces[i+2].Position-faces[i].Position);
					if (glm::dot(n,n) > 0.f) n = glm::normalize(n);
					for (int k = 0; k < 3; ++k) faces[i+k].Normal = n;
				}
				vertices = std::move(faces);
				weights = std::move(faceWeights);
				std::iota(indices.begin(), indices.end(), 0u);
			}
			if (tangents.empty() && !uv.empty()) {
				for (size_t i = 0; i < indices.size(); i += 3) {
					MeshVertex& a = vertices.at(indices[i]);
					MeshVertex& b = vertices.at(indices[i+1]);
					MeshVertex& c = vertices.at(indices[i+2]);
					glm::vec2 ab = b.TexCoord-a.TexCoord, ac = c.TexCoord-a.TexCoord;
					float det = ab.x*ac.y - ab.y*ac.x;
					if (det == 0.f) continue;
					glm::vec3 t = ((b.Position-a.Position)*ac.y - (c.Position-a.Position)*ab.y) / det;
					a.Tangent += t; b.Tangent += t; c.Tangent += t;
				}
			}
			return std::make_unique<SkinnedMesh>(jointCount, std::move(vertices), std::move(weights), std::move(indices));
		}
	}

	struct ModelAnimation::Data
	{
		struct Node
		{
			int Parent;
			glm::mat4 Matrix;
			bool HasMatrix;
		};
		std::vector<NodePose> Rest;
		std::vector<Node> Nodes;
		std::vector<size_t> Order;
		std::vector<Primitive> Parts;
		std::vector<std::vector<Track>> Tracks;
	};

	ModelAnimation::ModelAnimation() : m_Data(std::make_unique<Data>()) {}
	ModelAnimation::~ModelAnimation() = default;

	Ref<ModelAnimation> ModelAnimation::Load(const std::filesystem::path& path)
	{
		cgltf_options options{};
		options.file.read = ReadFile;
		std::string filename = path.u8string();
		cgltf_data* file = nullptr;
		if (cgltf_parse_file(&options, filename.c_str(), &file) != cgltf_result_success) return nullptr;
		Ref<ModelAnimation> asset(new ModelAnimation);
		Data& data = *asset->m_Data;
		std::unique_ptr<cgltf_data, decltype(&cgltf_free)> source(file, cgltf_free);
		if (!file->animations_count || cgltf_load_buffers(&options, file, filename.c_str()) != cgltf_result_success ||
			cgltf_validate(file) != cgltf_result_success) return nullptr;
		try
		{
			// 只留下每帧需要的关节、顶点和动作。GLB 里原始的图像与文件字节在 Load 返回时释放。
			for (size_t i = 0; i < file->nodes_count; ++i) {
				const cgltf_node& node = file->nodes[i];
				data.Rest.push_back({ glm::make_vec3(node.translation), glm::make_vec3(node.scale), Rotation(glm::make_vec4(node.rotation)) });
				data.Nodes.push_back({ node.parent ? int(node.parent-file->nodes) : -1, glm::make_mat4(node.matrix), bool(node.has_matrix) });
			}
			std::function<void(const cgltf_node&, bool)> visit = [&](const cgltf_node& node, bool active) {
				size_t index = &node - file->nodes;
				data.Order.push_back(index);
				if (active && node.mesh) for (size_t p = 0; p < node.mesh->primitives_count; ++p)
				{
					const cgltf_primitive& primitive = node.mesh->primitives[p];
					if (primitive.type != cgltf_primitive_type_triangles || primitive.targets_count)
						throw std::runtime_error("Model animation requires triangle meshes without morph targets");
					Primitive part{index};
					if (node.skin) {
						std::vector<float> inverse = Read(node.skin->inverse_bind_matrices);
						for (size_t j = 0; j < node.skin->joints_count; ++j) {
							part.Joints.push_back(node.skin->joints[j] - file->nodes);
							part.InverseBind.push_back(inverse.empty() ? glm::mat4(1.f) : glm::make_mat4(&inverse[16*j]));
						}
						part.Skin = ReadSkin(primitive, part.Joints.size());
					}
					data.Parts.push_back(std::move(part));
				}
				for (size_t c = 0; c < node.children_count; ++c) visit(*node.children[c], active);
			};
			const cgltf_scene* scene = file->scene ? file->scene : (file->scenes_count ? file->scenes : nullptr);
			if (scene) for (size_t i = 0; i < scene->nodes_count; ++i) visit(*scene->nodes[i], true);
			for (size_t i = 0; i < file->nodes_count; ++i)
				if (!file->nodes[i].parent && std::find(data.Order.begin(), data.Order.end(), i) == data.Order.end())
					visit(file->nodes[i], !scene);
			for (size_t a = 0; a < file->animations_count; ++a)
			{
				const cgltf_animation& animation = file->animations[a];
				Clip clip{animation.name ? animation.name : "Clip " + std::to_string(a+1), 0.f};
				std::vector<Track> tracks;
				for (size_t c = 0; c < animation.channels_count; ++c)
				{
					const cgltf_animation_channel& channel = animation.channels[c];
					const cgltf_animation_sampler& sampler = *channel.sampler;
					if (!channel.target_node || channel.target_path == cgltf_animation_path_type_weights) continue;
					Track track{size_t(channel.target_node-file->nodes), channel.target_path, sampler.interpolation};
					track.Times = Read(sampler.input);
					std::vector<float> output = Read(sampler.output);
					size_t width = channel.target_path == cgltf_animation_path_type_rotation ? 4 : 3;
					if (track.Times.empty() || output.size() != width*track.Times.size()*
						(sampler.interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1))
						throw std::runtime_error("Invalid glTF animation keys");
					for (size_t i = 0; i < output.size(); i += width)
						track.Values.emplace_back(output[i],output[i+1],output[i+2],width == 4 ? output[i+3] : 0.f);
					clip.Duration = std::max(clip.Duration, track.Times.back());
					tracks.push_back(std::move(track));
				}
				if (!tracks.empty()) {
					const std::string original = clip.Name;
					int suffix = 2;
					while (std::any_of(asset->m_Clips.begin(), asset->m_Clips.end(), [&](const Clip& other) { return other.Name == clip.Name; }))
						clip.Name = original + " (" + std::to_string(suffix++) + ")";
					asset->m_Clips.push_back(std::move(clip));
					data.Tracks.push_back(std::move(tracks));
				}
			}
		}
		catch (const std::exception& e) {
			RF_CORE_WARN("ModelAnimation: {0} ({1})", filename, e.what());
			return nullptr;
		}
		return asset->m_Clips.empty() || data.Parts.empty() ? nullptr : asset;
	}

	const std::vector<uint32_t>& ModelAnimation::GetIndices(size_t part) const
	{
		static const std::vector<uint32_t> empty;
		const Primitive& primitive = m_Data->Parts.at(part);
		return primitive.Skin ? primitive.Skin->GetIndices() : empty;
	}

	std::vector<ModelAnimation::Part> ModelAnimation::Sample(size_t clip, float seconds) const
	{
		std::vector<NodePose> local = m_Data->Rest;
		for (const Track& track : m_Data->Tracks.at(clip))
		{
			glm::vec4 value = SampleTrack(track, seconds);
			NodePose& pose = local[track.Node];
			switch (track.Path) {
				case cgltf_animation_path_type_translation: pose.Position = value; break;
				case cgltf_animation_path_type_rotation: pose.Rotation = glm::normalize(Rotation(value)); break;
				case cgltf_animation_path_type_scale: pose.Scale = value; break;
			}
		}
		std::vector<glm::mat4> world(local.size());
		for (size_t i : m_Data->Order) {
			const Data::Node& node = m_Data->Nodes[i];
			const NodePose& pose = local[i];
			glm::mat4 transform = node.HasMatrix ? node.Matrix :
				glm::translate(glm::mat4(1.f), pose.Position) * glm::mat4_cast(pose.Rotation) * glm::scale(glm::mat4(1.f), pose.Scale);
			world[i] = node.Parent < 0 ? transform : world[node.Parent] * transform;
		}
		std::vector<Part> result;
		for (const Primitive& part : m_Data->Parts)
		{
			Part pose;
			if (part.Skin) {
				std::vector<glm::mat4> palette(part.Joints.size());
				for (size_t j = 0; j < palette.size(); ++j)
					palette[j] = world[part.Joints[j]] * part.InverseBind[j];
				pose.Vertices = part.Skin->Deform(palette);
			} else pose.Transform = world[part.Node];
			result.push_back(std::move(pose));
		}
		return result;
	}
}
