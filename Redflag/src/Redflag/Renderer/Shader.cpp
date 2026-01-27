#include "rfpch.h"
#include "Shader.h"
#include"Log.h"

#include<glad/glad.h>

#include<glm/gtc/type_ptr.hpp>

namespace Redflag {
	Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		//---------------编译vertexshader-------------------------------------//
		//1.分配vertexshader内存
		// 相较于glGen，glCreate不仅创建了唯一id，还在GPU上分配了内存
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);

		//2.把shader代码传递给vertexshader
		const GLchar* source = vertexSrc.c_str();
		glShaderSource(vertexShader, 1, &source, 0);

		//3.编译vertexshader,并检查是否失败
		glCompileShader(vertexShader);
		GLint isCompiled = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			//get日志系统，存储在infoLog
			GLint maxlength = 0;
			glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxlength);
			std::vector<GLchar> infoLog(maxlength);
			glGetShaderInfoLog(vertexShader, maxlength, &maxlength, &infoLog[0]);
			
			glDeleteShader(vertexShader);
			RF_CORE_ERROR("{0}", infoLog.data());
			RF_CORE_ASSERT(false, "Vertex shader compilation failed!")
			
			return;
		}
		//---------------编译vertexshader-------------------------------------//




		//---------------编译fragementshader-------------------------------------//
		// 片段着色器一样
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		// Send the fragment shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		source = fragmentSrc.c_str();
		glShaderSource(fragmentShader, 1, &source, 0);
		glCompileShader(fragmentShader);
		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE) {
			GLint maxLength = 0;
			glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);
			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(fragmentShader);
			// Either of them. Don't leak shaders.
			glDeleteShader(vertexShader);
			RF_CORE_ERROR("{0}", infoLog.data());
			RF_CORE_ASSERT(false, "Fragment shader compilation failed!");
			
			return;
		}
		//---------------编译fragmentshader-------------------------------------//




		//---------------link成program-------------------------------------//
		m_RendererID = glCreateProgram();
		GLuint program = m_RendererID;

		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);

		//link our program
		glLinkProgram(program);


		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE) {
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);
			// We don't need the program anymore.
			glDeleteProgram(program);
			// Don't leak shaders either.
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);
			RF_CORE_ERROR("{0}", infoLog.data());
			RF_CORE_ASSERT(false, "Shader link failed!");
			return;
		}
		//---------------link成program-------------------------------------//
	}
	Shader::~Shader()
	{
		glDeleteProgram(m_RendererID);
	}
	void Shader::Bind() const
	{
		glUseProgram(m_RendererID);
	}
	void Shader::Unbind() const
	{
		glUseProgram(0);
	}

	void Shader::UploadUniformFloat4(const std::string& name, const glm::vec4& values)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform4f(location, values.x, values.y, values.z, values.w);
	}

	//把matirx上传给对应着色器的对应name的uniform变量
	void Shader::UploadUniformMat4(const std::string& name,const glm::mat4& matrix)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}
}