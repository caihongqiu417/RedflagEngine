#include "rfpch.h"
#include "OpenGLShader.h"
#include"Log.h"
#include<fstream>
#include<glad/glad.h>

#include<glm/gtc/type_ptr.hpp>

namespace Redflag {
	static GLenum ShaderTypeFromString(const std::string& type)
	{
		if (type == "vertex")
			return GL_VERTEX_SHADER;
		if (type == "fragment")
			return GL_FRAGMENT_SHADER;
		RF_CORE_ASSERT(false, "Unknown shader type!");
		return 0;
	}

	OpenGLShader::OpenGLShader(const std::string& filepath)
	{
		//1.读取文件内容到字符串
		std::string shaderSource=ReadFile(filepath);
		//2.预处理字符串，分离出vertexshader和fragmentshader代码
		std::unordered_map<GLenum, std::string> shaderSources = PreProcess(shaderSource);
		//3.编译shader
		Compile(shaderSources);
	}

	OpenGLShader::OpenGLShader(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		//1.把vertexshader和fragmentshader代码存储到unordered_map中
		std::unordered_map<GLenum, std::string> shaderSources;
		//2.编译shader
		shaderSources[GL_VERTEX_SHADER] = vertexSrc;
		shaderSources[GL_FRAGMENT_SHADER] = fragmentSrc;
		Compile(shaderSources);
	}

	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_RendererID);
	}


	std::string OpenGLShader::ReadFile(const std::string& filepath)
	{
		std::string result;
		//以二进制方式打开文件，读取到末尾，获取文件大小，分配字符串内存，重新回到开头，读取内容到字符串
		std::ifstream in(filepath, std::ios::in | std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			result.resize(in.tellg());
			in.seekg(0, std::ios::beg);
			in.read(&result[0], result.size());
			in.close();
		}
		else
		{
			RF_CORE_ERROR("Could not open file '{0}'", filepath);
		}
		return result;
	}

	std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
	{
		std::unordered_map<GLenum, std::string> shaderSources;

		//定义一个token，标识shader类型的开始位置
		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = source.find(typeToken, 0);
		//找到第一个#type后，循环处理剩下的shader代码，直到没有#type为止
		while (pos != std::string::npos)
		{
			//1.找到#type后面第一个换行符的位置，获取shader类型字符串
			size_t eol = source.find_first_of("\r\n", pos);
			RF_CORE_ASSERT(eol != std::string::npos, "Syntax error");
			size_t begin = pos + typeTokenLength + 1;
			std::string type = source.substr(begin, eol - begin);
			RF_CORE_ASSERT(type == "vertex" || type == "fragment", "Invalid shader type specified");
			//2.找到下一个#type的位置，获取shader代码字符串
			size_t nextLinePos = source.find_first_not_of("\r\n", eol);
			pos = source.find(typeToken, nextLinePos);
			//3.把shader类型和代码存储到unordered_map中，继续循环处理下一个#type
			shaderSources[ShaderTypeFromString(type)] = source.substr(nextLinePos, pos - (nextLinePos == std::string::npos ? source.size() - 1 : nextLinePos));
		}
		return shaderSources;
	}

	// 编译并链接Shader程序
	void OpenGLShader::Compile(const std::unordered_map<GLenum, std::string>& shaderSources)
	{
		// 创建OpenGL着色器程序对象
		GLuint program = glCreateProgram();

		RF_CORE_ASSERT(shaderSources.size() <= 2, "We only support 2 shaders for now (vertex and fragment)");
		// 存储所有创建的着色器ID，便于后续管理
		std::array<GLenum, 2> glShaderIDs;
		int glShaderIDIndex = 0;

		// 1. 编译每个着色器并附加到程序
		for (auto& kv : shaderSources)
		{
			GLenum type = kv.first;           // 着色器类型（如GL_VERTEX_SHADER）
			const std::string& source = kv.second;  // 着色器源代码

			// 创建着色器对象
			GLuint shader = glCreateShader(type);

			// 设置着色器源代码
			const GLchar* sourceCStr = source.c_str();
			glShaderSource(shader, 1, &sourceCStr, 0);

			// 编译着色器
			glCompileShader(shader);

			// 检查编译状态
			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

			if (isCompiled == GL_FALSE)
			{
				// 获取错误信息长度
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

				// 获取错误信息
				std::vector<GLchar> infoLog(maxLength);
				glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

				// 清理资源
				glDeleteShader(shader);

				// 输出错误信息
				RF_CORE_ERROR("{0}", infoLog.data());
				RF_CORE_ASSERT(false, "Shader compilation failed!");
				return;
			}

			// 将成功编译的着色器附加到程序
			glAttachShader(program, shader);
			glShaderIDs[glShaderIDIndex++] = shader;	
		}

		// 2. 链接程序并检查链接状态
		glLinkProgram(program);

		GLint isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);

		if (isLinked == GL_FALSE)
		{
			// 获取链接错误信息长度
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			// 获取链接错误信息
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

			// 清理所有资源
			glDeleteProgram(program);
			for (auto id : glShaderIDs)
				glDeleteShader(id);

			// 输出错误信息
			RF_CORE_ERROR("{0}", infoLog.data());
			RF_CORE_ASSERT(false, "Shader link failed!");
			return;
		}

		// 3. 清理临时资源（分离并删除着色器对象）
		for (auto id : glShaderIDs)
		{
			glDetachShader(program, id);  // 从程序中分离着色器
			glDeleteShader(id);           // 删除着色器对象
		}

		// 保存最终的程序ID
		m_RendererID = program;
	}

	void OpenGLShader::Bind() const
	{
		glUseProgram(m_RendererID);
	}
	void OpenGLShader::Unbind() const
	{
		glUseProgram(0);
	}

	void OpenGLShader::UploadUniformInt(const std::string& name, int value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1i(location, value);
	}

	void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1f(location, value);
	}

	void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& values)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform2f(location, values.x, values.y);
	}

	void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& values)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform3f(location, values.x, values.y, values.z);
	}

	void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& values)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform4f(location, values.x, values.y, values.z, values.w);
	}

	//把matirx上传给对应着色器的对应name的uniform变量
	void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}
}