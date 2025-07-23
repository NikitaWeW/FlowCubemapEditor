/*
        +____________+
        /:\         ,:\
       / : \       , : \
      /  :  \     ,  :  \
     /   :   +-----------+
    +....:../:...+   :  /|
    |\   +./.:...`...+ / |
    | \ ,`/  :   :` ,`/  |
    |  \ /`. :   : ` /`  |
    | , +-----------+  ` |
    |,  |   `+...:,.|...`+
    +...|...,'...+  |   /
     \  |  ,     `  |  /
      \ | ,       ` | /
       \|,         `|/
        +___________+

2-Dimensional ASCII Representation Of A 3-Dimensional Cross-Section Of A 4-Dimensional Cube
*/

#include "glad/gl.h"
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"

#include "logger.h"
#include "tiny_obj_loader.h"
#include "ease_functions.hpp"

#include "opengl/Framebuffer.hpp"
#include "opengl/Texture.hpp"
#include "opengl/IndexBuffer.hpp"
#include "opengl/VertexBuffer.hpp"
#include "opengl/Shader.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <iostream>
#include <stdexcept>

struct Mesh
{
    ogl::VertexBuffer vbo;
    ogl::VertexArray vao;
    unsigned count = 0;
};
template <typename T>
struct VelocityVariable
{
    ease::easeFuncPtr<T> easeFunc = ease::outCirc<T>;
    T velocity = T{0};
    T value = T{0};
    T falloff = T{1};
    glm::vec2 edges{0.0f, 10.0f};

    inline void update(float deltatime)
    {
        value += velocity * deltatime;
        T x = glm::clamp((glm::abs(velocity) - T{edges.x}) / (T{edges.y} - T{edges.x}), T{0}, T{1});
        T curve = easeFunc(x);
        velocity -= velocity * curve * deltatime * falloff;
    }
};
struct Data
{
    GLFWwindow *window = nullptr;

    glm::ivec2 windowSize{-1};
    glm::dvec2 mousePos{0};
    glm::dvec2 prevMousePos{0};

    glm::mat4 viewMat{1.0f};
    glm::mat4 projMat{1.0f};

    glm::vec3 cameraPos{0};
    glm::vec3 cameraDir{1};

    glm::vec3 intersectionPoint;
    glm::vec3 prevIntersectionPoint{-1};

    unsigned flowMapSize = -1;
    std::array<ogl::Cubemap, 2> pinPongCurrentDrawFlowmaps;
    ogl::Cubemap flowMap;
    unsigned char currentPinPongCurrentDrawFlowmap = 0;

    ogl::ShaderProgram flowMapDrawShader;
    ogl::ShaderProgram blurFlowMapShader;
    ogl::ShaderProgram combineShader;

    float deltatime = 0.1;

    VelocityVariable<glm::vec2> yawPitch{.value = glm::vec2{0}};
    VelocityVariable<float> distance{.value = 3};
    float sensitivity = 500;

    struct {
        bool showFlow = false;
        bool hdrFlowmap = true;
    } inputs;
};

int main(int argc, char **argv);

constexpr unsigned NUM_SAMPLES = 4;
constexpr std::string_view CONFIG_WINDOW_NAME = "Dear ImGui Demo"; // placeholder
constexpr float ZNEAR = 0.01;
constexpr float ZFAR = 100;
constexpr float CUBE_MODEL_SIZE = 1.0f; 

void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::Texture &texture, glm::ivec2 size, GLenum attachment = GL_COLOR_ATTACHMENT0);
void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::TextureMS &texture, glm::ivec2 size, GLenum attachment = GL_COLOR_ATTACHMENT0);
bool init(GLFWwindow **window);
Mesh load(std::string_view path);
void processInput(Data &data);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

int main(int argc, char **argv)
{
    GLFWwindow *window = nullptr;
    if(!init(&window)) {
        LOG_FATAL("failed to init!");
        return -1;
    }
    assert(window);

    // ===================================

    ogl::Cubemap skybox{"res/textures/qwantani_dawn_puresky_4k.hdr"};
    ogl::Texture flowTexture{"res/textures/water.jpg"};

    ogl::ShaderProgram cubeShader{"shaders/prop"};
    ogl::ShaderProgram displayShader{"shaders/hdrImage"};
    ogl::ShaderProgram skyboxShader{"shaders/skybox"};
    ogl::ShaderProgram gridShader{"shaders/grid"};

    Mesh cube = load("res/models/cube.obj");

    ogl::Framebuffer mainFBO;
    ogl::Renderbuffer mainRBO{0};
    ogl::TextureMS mainColor{GL_LINEAR, GL_CLAMP_TO_EDGE};

    ogl::Framebuffer displayFBO;
    ogl::Renderbuffer displayRBO{0};
    ogl::Texture displayTexture{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE};

    // ===================================

    Data data{};
    data.window = window;
    data.distance.falloff = 10;

    data.flowMapSize = 1024;
    for(unsigned i = 0; i < data.pinPongCurrentDrawFlowmaps.size(); ++i)
        data.pinPongCurrentDrawFlowmaps[i] = ogl::Cubemap{0};
    glTextureStorage2D(
        data.pinPongCurrentDrawFlowmaps[0].getRenderID(),
        1,
        GL_RGBA16F,
        data.flowMapSize,
        data.flowMapSize
    );
    glTextureStorage2D(
        data.pinPongCurrentDrawFlowmaps[1].getRenderID(),
        1,
        GL_RGBA16F,
        data.flowMapSize,
        data.flowMapSize
    );

    data.flowMap = ogl::Cubemap{0};
    glTextureStorage2D(
        data.flowMap.getRenderID(),
        1,
        GL_RGBA16F,
        data.flowMapSize,
        data.flowMapSize
    );

    data.flowMapDrawShader = ogl::ShaderProgram{"shaders/drawPoint"};
    data.blurFlowMapShader = ogl::ShaderProgram{"shaders/blurCubemap"};
    data.combineShader = ogl::ShaderProgram{"shaders/combine"};

    // ===================================
    
    glfwSetWindowUserPointer(data.window, &data);
    glfwSetScrollCallback(data.window, scroll_callback);

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!glfwWindowShouldClose(window))
    {
        auto start = std::chrono::high_resolution_clock::now();
        glm::ivec2 prevDim = data.windowSize;
        glfwGetFramebufferSize(window, &data.windowSize.x, &data.windowSize.y);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiIO &io = ImGui::GetIO();

        glViewport(0, 0, data.windowSize.x, data.windowSize.y);
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("Editor DockSpace");
            ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        }
        
        if(data.windowSize != prevDim)
        { // resize drawbuffers
            resizeColorAttachment(mainFBO, mainColor, data.windowSize);
            glNamedRenderbufferStorageMultisample(mainRBO.getRenderID(), NUM_SAMPLES, GL_DEPTH24_STENCIL8, data.windowSize.x, data.windowSize.y);

            resizeColorAttachment(displayFBO, displayTexture, data.windowSize);
            glNamedRenderbufferStorage(displayRBO.getRenderID(), GL_DEPTH24_STENCIL8, data.windowSize.x, data.windowSize.y);
        }
        if(mainFBO.getRenderID() == 0)
        {
            mainFBO = ogl::Framebuffer{0}; // dummy argument
            mainFBO.attach(mainColor, GL_COLOR_ATTACHMENT0);
            mainFBO.attach(mainRBO, GL_DEPTH_STENCIL_ATTACHMENT);
            assert(mainFBO.isComplete());
        }
        if(displayFBO.getRenderID() == 0)
        {
            displayFBO = ogl::Framebuffer{0};
            displayFBO.attach(displayTexture, GL_COLOR_ATTACHMENT0);
            displayFBO.attach(displayRBO, GL_DEPTH_STENCIL_ATTACHMENT);
            assert(displayFBO.isComplete());
        }
        processInput(data);

        // __________________________
        // ==========================

        mainFBO.bind();

        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);

        // ==============
        // draw a skybox 
        // ==============

        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);

        skyboxShader.bind();
        skybox.bind(0);

        glUniformMatrix4fv(skyboxShader.getUniform("u_viewMat"),        1, GL_FALSE, glm::value_ptr(data.viewMat));
        glUniformMatrix4fv(skyboxShader.getUniform("u_projectionMat"),  1, GL_FALSE, glm::value_ptr(data.projMat));

        // vertices hard-coded in the shader
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 14);

        // ============
        // draw a grid 
        // ============

        glDepthFunc(GL_LESS);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        gridShader.bind();

        glUniformMatrix4fv(gridShader.getUniform("u_viewMat"),        1, GL_FALSE, glm::value_ptr(data.viewMat));
        glUniformMatrix4fv(gridShader.getUniform("u_projectionMat"),  1, GL_FALSE, glm::value_ptr(data.projMat));
        glUniform3fv(      gridShader.getUniform("u_cameraPosition"), 1, &data.cameraPos.x);

        // vertices hard-coded in the shader
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // ============
        // draw a cube 
        // ============

        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        cubeShader.bind();
        data.pinPongCurrentDrawFlowmaps[data.currentPinPongCurrentDrawFlowmap].bind(0);
        flowTexture.bind(1);
        
        glUniform1i(       cubeShader.getUniform("u_showFlow"),       data.inputs.showFlow);
        glUniform1i(       cubeShader.getUniform("u_hdrFlowMap"),     data.inputs.hdrFlowmap);
        glUniform1f(       cubeShader.getUniform("u_time"),           glfwGetTime());
        glUniformMatrix4fv(cubeShader.getUniform("u_modelMat"),       1, GL_FALSE, glm::value_ptr(glm::mat4{1.0f}));
        glUniformMatrix4fv(cubeShader.getUniform("u_viewMat"),        1, GL_FALSE, glm::value_ptr(data.viewMat));
        glUniformMatrix4fv(cubeShader.getUniform("u_projectionMat"),  1, GL_FALSE, glm::value_ptr(data.projMat));
        
        cube.vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, cube.count);

        // ============================================
        // draw to a display texture + post processing 
        // ============================================

        glDepthFunc(GL_ALWAYS);

        displayFBO.bind();
        displayShader.bind();
        mainColor.bind(0);
        // vertices hard-coded in the shader
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
        
        // ==========================
        // display a display texture  
        // ==========================

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDepthFunc(GL_ALWAYS);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glBlitNamedFramebuffer(displayFBO.getRenderID(), 0, 0, 0, data.windowSize.x, data.windowSize.y, 0, 0, data.windowSize.x, data.windowSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // ============
        // ImGui stuff 
        // ============
        
        ImGui::ShowDemoWindow();
        
        // __________________________.
        // ==========================|
        
        glfwPollEvents();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
        }
        glfwSwapBuffers(window);
        data.deltatime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1.0E-6;
    }
    
    glfwDestroyWindow(window);
    glfwTerminate();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, const void *objMesh)
{
    if(source == GL_DEBUG_SOURCE_SHADER_COMPILER && (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_OTHER)) return; // handled by ShaderProgram class 

    struct OpenGlError {
        GLuint id;
        std::string source;
        std::string type;
        std::string severity;
        std::string msg;
    } error;
    
    error.id = id;
    error.msg = msg;

    switch (source) {
        case GL_DEBUG_SOURCE_API:
        error.source = "api";
        break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        error.source = "window system";
        break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
        error.source = "shader compiler";
        break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
        error.source = "third party";
        break;

        case GL_DEBUG_SOURCE_APPLICATION:
        error.source = "application";
        break;

        case GL_DEBUG_SOURCE_OTHER:
        error.source = "unknown";
        break;

        default:
        error.source = "unknown";
        break;
    }
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
        error.type = "error";
        break;

        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        error.type = "deprecated behavior warning";
        break;

        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        error.type = "udefined behavior warning";
        break;

        case GL_DEBUG_TYPE_PORTABILITY:
        error.type = "portability warning";
        break;

        case GL_DEBUG_TYPE_PERFORMANCE:
        error.type = "performance warning";
        break;

        case GL_DEBUG_TYPE_OTHER:
        error.type = "message";
        break;

        case GL_DEBUG_TYPE_MARKER:
        error.type = "marker message";
        break;

        default:
        error.type = "unknown message";
        break;
    }
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
        error.severity = "high";
        break;

        case GL_DEBUG_SEVERITY_MEDIUM:
        error.severity = "medium";
        break;

        case GL_DEBUG_SEVERITY_LOW:
        error.severity = "low";
        break;

        case GL_DEBUG_SEVERITY_NOTIFICATION:
        error.severity = "notification";
        break;

        default:
        error.severity = "unknown";
        break;
    }

    LOG_WARN("%d: opengl %s severity %s, raised from %s:\n\t%s", 
            error.id, 
            error.severity.c_str(), 
            error.type.c_str(), 
            error.source.c_str(), 
            error.msg.c_str());
}
void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::TextureMS &texture, glm::ivec2 size, GLenum attachment)
{
    glDeleteTextures(1, &texture.getRenderID());
    texture.getRenderID() = 0;
    glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &texture.getRenderID());
    glTextureStorage2DMultisample(texture.getRenderID(), NUM_SAMPLES, GL_RGBA16F, size.x, size.y, true);
    if(fbo.getRenderID() != 0)
    {
        fbo.attach(texture, GL_COLOR_ATTACHMENT0);
        assert(fbo.isComplete());
    }
}
void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::Texture &texture, glm::ivec2 size, GLenum attachment)
{
    glDeleteTextures(1, &texture.getRenderID());
    texture.getRenderID() = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture.getRenderID());
    glTextureStorage2D(texture.getRenderID(), 1, GL_RGBA16F, size.x, size.y);
    if(fbo.getRenderID() != 0)
    {
        fbo.attach(texture, GL_COLOR_ATTACHMENT0);
        assert(fbo.isComplete());
    }
}
bool init(GLFWwindow **window)
{
    if (!glfwInit()) {
        LOG_FATAL("failed to initialize glfw!");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, NUM_SAMPLES);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWvidmode const *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    *window = glfwCreateWindow(mode->width * 0.5, mode->height * 0.5, "opengl", nullptr, nullptr);
    glfwSetWindowTitle(*window, "flow cubemap editor v0.0 (still broken)");

    if (!*window) {
        LOG_FATAL("failed to initialize window.");
        return false;
    }
    glfwMakeContextCurrent(*window);
    if (!gladLoadGL((GLADloadfunc) glfwGetProcAddress)) {
        LOG_FATAL("gladLoadGL: Failed to initialize GLAD!");
        return false;
    }
    
    ImGui::CreateContext();
    IMGUI_CHECKVERSION();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if(getenv("WAYLAND_DISPLAY")) 
        LOG_INFO("wayland detected! imgui multiple viewports feature is not supported!");
    else 
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplGlfw_InitForOpenGL(*window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui::StyleColorsDark();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);
    LOG_DEBUG("running in debug mode!");
    
    glfwSwapInterval(0);

    return true;
}
Mesh load(std::string_view path)
{
    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = "./";
    tinyobj::ObjReader reader;

    if(!reader.ParseFromFile(std::string{path}, config)) {
        LOG_ERROR("failed to load \"%s\"!", path.data());
        if(!reader.Error().empty()) {
            LOG_ERROR(reader.Error().c_str());
        }
        return Mesh{
            .count = 0
        };
    }

    if(!reader.Warning().empty()) {
        LOG_WARN(reader.Warning().c_str());
    }

    auto &attrib = reader.GetAttrib();
    auto &shapes = reader.GetShapes();
    // auto &materials = reader.GetMaterials();

    std::vector<glm::vec3> positions{};
    std::vector<glm::vec3> normals  {};
    std::vector<glm::vec2> texcoords{};

    Mesh mesh{};
    mesh.count = 0;

    // "unzip" the object by unpacing the indices
    for(auto &shape : shapes) {
        size_t index_offset = 0;
        for(auto &face : shape.mesh.num_face_vertices) {
            for(size_t vertex = 0; vertex < face; ++vertex) {
                // access to vertex
                tinyobj::index_t idx = shape.mesh.indices[index_offset + vertex];
                assert(idx.texcoord_index >= 0);
                assert(idx.normal_index >= 0);

                positions.emplace_back(
                    attrib.vertices[3*size_t(idx.vertex_index)+0],
                    attrib.vertices[3*size_t(idx.vertex_index)+1],
                    attrib.vertices[3*size_t(idx.vertex_index)+2] 
                );
                normals.emplace_back(
                    attrib.normals[3*size_t(idx.normal_index)+0],
                    attrib.normals[3*size_t(idx.normal_index)+1],
                    attrib.normals[3*size_t(idx.normal_index)+2]
                );
                texcoords.emplace_back(
                    attrib.texcoords[2*size_t(idx.texcoord_index)+0],
                    attrib.texcoords[2*size_t(idx.texcoord_index)+1] 
                );

                ++mesh.count;
            }
            index_offset += face;
            // shape.mesh.material_ids[face]; // material
        }
    }

    mesh.vbo = ogl::VertexBuffer{
        positions.size() * sizeof(decltype(positions[0])) +
        normals.size()   * sizeof(decltype(normals[0])) +
        texcoords.size() * sizeof(decltype(texcoords[0]))
    };

    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        0, 
        positions.size() * sizeof(decltype(positions[0])), 
        positions.data()
    );
    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        positions.size() * sizeof(decltype(positions[0])), 
        normals.size()   * sizeof(decltype(normals[0])),
        normals.data()
    );
    glNamedBufferSubData(mesh.vbo.getRenderID(), 
        positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0])), 
        texcoords.size() * sizeof(decltype(texcoords[0])),
        texcoords.data()
    );

    ogl::VertexBufferLayout layout = {
        {3, GL_FLOAT, 0},
        {3, GL_FLOAT, positions.size() * sizeof(decltype(positions[0]))},
        {2, GL_FLOAT, positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0]))}
    };
    
    mesh.vao = ogl::VertexArray{mesh.vbo, layout};

    return mesh;
}
void updateVP(Data &data, bool cameraLocked)
{
    data.yawPitch.update(data.deltatime);
    data.yawPitch.falloff = glm::mix(glm::vec2{10.0f}, glm::vec2{5.0f}, static_cast<float>(!cameraLocked));
    data.distance.update(data.deltatime);
    data.distance.value = glm::clamp<float>(data.distance.value, 1, 5);

    data.viewMat = glm::mat4{1.0f};
    data.viewMat = glm::translate(
        data.viewMat,
        glm::vec3{0, 0, -data.distance.value}
    );
    data.viewMat = glm::rotate(
        data.viewMat,
        glm::radians(data.yawPitch.value.y),
        glm::vec3{1, 0, 0}
    );
    data.viewMat = glm::rotate(
        data.viewMat,
        glm::radians(data.yawPitch.value.x),
        glm::vec3{0, 1, 0}
    );
    data.projMat = glm::perspective<float>(glm::radians(45.0f), (float) data.windowSize.x / data.windowSize.y, 0.01, 100);

    data.cameraPos = glm::inverse(data.viewMat) * glm::vec4{0, 0, 0, 1};
    data.cameraDir = glm::inverse(data.viewMat) * glm::vec4{0, 0,-1, 0};
}
glm::vec3 unProjectMouse(Data &data)
{
    return glm::unProject (
        glm::vec3 {
            glm::vec2{data.mousePos.x, data.windowSize.y - data.mousePos.y},
            1.0f
        }, 
        data.viewMat, data.projMat, 
        glm::vec4{0, 0, data.windowSize.x, data.windowSize.y}
    );
}
// https://gist.github.com/DomNomNom/46bb1ce47f68d255fd5d
glm::vec2 rayAABB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 boxMin, glm::vec3 boxMax) {
    glm::vec3 tMin = (boxMin - rayOrigin) / rayDir;
    glm::vec3 tMax = (boxMax - rayOrigin) / rayDir;
    glm::vec3 t1 = glm::min(tMin, tMax);
    glm::vec3 t2 = glm::max(tMin, tMax);
    float tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
    float tFar = glm::min(glm::min(t2.x, t2.y), t2.z);
    return glm::vec2(tNear, tFar);
};
void processInput(Data &data)
{
    assert(data.window);

    ImGui::Begin(CONFIG_WINDOW_NAME.data());
    bool cameraLocked = glfwGetMouseButton(data.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !ImGui::IsWindowFocused();
    ImGui::End();
    glfwSetInputMode(data.window, GLFW_CURSOR, cameraLocked ? GLFW_CURSOR_CAPTURED : GLFW_CURSOR_NORMAL);
    
    glfwGetCursorPos(data.window, &data.mousePos.x, &data.mousePos.y);
    glm::vec2 deltaMouse = data.prevMousePos - data.mousePos;
    deltaMouse.x = -deltaMouse.x;
    data.prevMousePos = data.mousePos;

    if(cameraLocked) 
    {
        data.yawPitch.velocity += glm::vec2{deltaMouse.x, -deltaMouse.y} * data.deltatime * data.sensitivity;
    }

    updateVP(data, cameraLocked);
    
    if(glfwGetMouseButton(data.window, GLFW_MOUSE_BUTTON_LEFT) && !cameraLocked && (deltaMouse != glm::vec2{0}))
    {
        glm::vec3 point = unProjectMouse(data);

        glm::vec3 origin = data.cameraPos;
        glm::vec3 dir = glm::normalize(point - origin);
        
        glm::vec2 intersection = rayAABB(origin, dir, glm::vec3{-CUBE_MODEL_SIZE * 0.5f}, glm::vec3{CUBE_MODEL_SIZE * 0.5f});

        if(intersection.x <= intersection.y)
        {
            data.intersectionPoint = origin + dir * intersection.x;
            data.intersectionPoint /= CUBE_MODEL_SIZE * 0.5f;
            glm::vec3 prevPoint = data.prevIntersectionPoint == glm::vec3{0} ? data.intersectionPoint : data.prevIntersectionPoint;

            data.flowMapDrawShader.bind();
            glBindImageTexture(0, data.pinPongCurrentDrawFlowmaps[data.currentPinPongCurrentDrawFlowmap].getRenderID(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
            glUniform3fv(data.flowMapDrawShader.getUniform("u_point"),      1, glm::value_ptr(data.intersectionPoint));
            glUniform3fv(data.flowMapDrawShader.getUniform("u_prevPoint"),  1, glm::value_ptr(prevPoint));
            glUniform2fv(data.flowMapDrawShader.getUniform("u_deltaMouse"), 1, glm::value_ptr(deltaMouse / glm::vec2{10.0f})); // FIXME: insert something reasonable here
            glDispatchCompute((data.flowMapSize + 15) / 16, (data.flowMapSize + 7) / 8, 6);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            data.prevIntersectionPoint = data.intersectionPoint;

            for(unsigned i = 0; i < 4; ++i)
            {
                data.blurFlowMapShader.bind();
                glBindImageTexture(0, data.pinPongCurrentDrawFlowmaps[data.currentPinPongCurrentDrawFlowmap].getRenderID(),  0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA16F);
                glBindImageTexture(1, data.pinPongCurrentDrawFlowmaps[!data.currentPinPongCurrentDrawFlowmap].getRenderID(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                glDispatchCompute((data.flowMapSize + 15) / 16, (data.flowMapSize + 7) / 8, 6);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

                data.currentPinPongCurrentDrawFlowmap = !data.currentPinPongCurrentDrawFlowmap;
            }
        }
    }
    else 
    {
        data.prevIntersectionPoint = glm::vec3{0};
    }
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    Data &data = *static_cast<Data *>(glfwGetWindowUserPointer(window));
    ImGui::Begin(CONFIG_WINDOW_NAME.data());
    if(ImGui::IsWindowFocused()) {
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    } else {
        data.distance.velocity -= yoffset * data.deltatime * data.sensitivity;
    }
    ImGui::End();
}
