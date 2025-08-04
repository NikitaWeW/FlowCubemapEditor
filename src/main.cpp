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

---

Copyright (c) 2025 Nikita Martynau (https://opensource.org/license/mit)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "glad/gl.h"
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_glfw.h"
#include "imgui_stdlib.h"

#include "stb_image.h"
#include "stb_image_write.h"
// #include "logger.h"
#include "Bitmap.hpp"
#include "equirect.hpp"
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
#include <algorithm>
#include <sstream>
#include <queue>

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
enum SaveType : int
{
    PNG = 0, HDR = 1
};
enum SaveLayout : int 
{ 
    UNWRAPPED = 0, SIX_IMAGES = 1, EQUIRECTANGULAR = 2, ONE_IMAGE
};

// for a small application like this i think its fine to use a single struct as an app state
struct Data
{
    GLFWwindow *window = nullptr;

    glm::ivec2 windowSize{-1};
    glm::dvec2 mousePos{0};
    glm::dvec2 prevMousePos{0};
    glm::vec2  deltaMouse{0};

    glm::mat4 viewMat{1.0f};
    glm::mat4 projMat{1.0f};
    glm::mat2 horizontalRotation;

    glm::vec3 cameraPos{0};
    glm::vec3 cameraDir{1};

    glm::vec3 intersectionPoint;
    glm::vec3 prevIntersectionPoint{-1};

    unsigned flowMapSize = -1;
    ogl::Cubemap flowMap;
    ogl::Cubemap texture;

    ogl::ShaderProgram flowMapDrawShader;
    ogl::ShaderProgram flowMapClearShader;
    ogl::ShaderProgram cubemapBlurShader;

    float deltatime = 0.1;

    VelocityVariable<glm::vec2> yawPitch{.value = glm::vec2{0}};
    VelocityVariable<float> distance{.value = 3};

    struct Inputs {
        int saveType = PNG;
        int saveLayout = UNWRAPPED;
        int textureLayout = ONE_IMAGE;
        
        int showFlow = false;
        bool blurPreview = true;
        bool eraseMode;
        bool hdrFlowmap = false;

        float sensitivity = 1;
        float brushSize = 0.1;
        float flowIntensity = 0.05;
        unsigned blurSteps = 100;

        std::string path = "flowmap.png";
        std::string texturePath = "res/textures/water.jpg";
    } inputs;
    
    std::queue<std::stringstream> messages;
};
class MessageStream : public std::stringstream
{
private:
    Data *m_data = nullptr;
public:
    inline ~MessageStream()
    {
        if(m_data)
        {
            if(rdbuf()->in_avail())
                m_data->messages.push(std::move(*this));
        }
        else
        {
            std::cout << "data not set in MessageStream!\n";
        }
    }

    inline void setData(Data &data) { m_data = &data; }
};

constexpr unsigned NUM_SAMPLES = 4;
constexpr std::string_view CONFIG_WINDOW_NAME = "Properties";
constexpr float ZNEAR = 0.01;
constexpr float ZFAR = 100;
constexpr float CUBE_MODEL_SIZE = 1.0f; 

void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::Texture &texture, glm::ivec2 size, GLenum attachment = GL_COLOR_ATTACHMENT0);
void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::TextureMS &texture, glm::ivec2 size, GLenum attachment = GL_COLOR_ATTACHMENT0);
bool init(GLFWwindow **window);
Mesh loadMesh(std::string_view path);
void processInput(Data &data);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
void helpMarker(const char *desc);
void clearFlowMap(Data &data);
void saveUnwrapped(Data &data);
void saveSixImages(Data &data);
void saveEquirectangular(Data &data);
bool loadUnwrapped(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess);
bool loadSixImages(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess);
bool loadEquirectangular(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess);
bool loadOneImage(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess);
void loadCustomTexture(Data &data);

int main(int argc, char **argv)
{
    GLFWwindow *window = nullptr;
    if(!init(&window)) {
        std::cout << "failed to init!\n";
        return -1;
    }
    assert(window);

    // ===================================
    Data data{};
    std::stringstream message;
    message 
    << "Hello!\n"
    << "This is Flow Cubemap Painter. It allows you to draw directions onto a cube, and import/export drawings using different layouts.\n";
    data.messages.push(std::move(message));

    message.clear();
    message
    << "You can dock the properties window by dragging it by the top header.\n"
    << "To orbit the cube, drag with the right mouse button or use the arrow keys.\n"
    << "Scroll to change the orbit radius.\n"
    << "Draw using the left mouse button.\n"
    << "Once you're done, press the 'Save As...' button, choose the layout, file type, and number of blur steps (0 means no blur), then save.\n";
    data.messages.push(std::move(message));

    ogl::Cubemap skybox{"res/textures/qwantani_dawn_puresky_4k.hdr"};
    loadCustomTexture(data);

    ogl::ShaderProgram cubeShader{"shaders/prop"};
    ogl::ShaderProgram displayShader{"shaders/hdrImage"};
    ogl::ShaderProgram skyboxShader{"shaders/skybox"};
    ogl::ShaderProgram gridShader{"shaders/grid"};
    data.flowMapDrawShader = ogl::ShaderProgram{"shaders/stroke"};
    data.flowMapClearShader = ogl::ShaderProgram{"shaders/clear"};
    data.cubemapBlurShader = ogl::ShaderProgram{"shaders/blurCubemap"};


    Mesh cube = loadMesh("res/models/cube.obj");

    ogl::Framebuffer mainFBO;
    ogl::Renderbuffer mainRBO{0};
    ogl::TextureMS mainColor{GL_LINEAR, GL_CLAMP_TO_EDGE};

    ogl::Framebuffer displayFBO;
    ogl::Renderbuffer displayRBO{0};
    ogl::Texture displayTexture{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE};

    // ===================================

    data.window = window;
    data.distance.falloff = 10;

    data.flowMapSize = 1024;

    data.flowMap = ogl::Cubemap{0};
    glTextureStorage2D(
        data.flowMap.getRenderID(),
        1,
        GL_RGBA32F,
        data.flowMapSize,
        data.flowMapSize
    );

    bool dontAskToClear = true;
    bool nextTime = false;

    // ===================================
    
    glfwSetWindowUserPointer(data.window, &data);
    glfwSetScrollCallback(data.window, scroll_callback);
    glfwSetKeyCallback(data.window, key_callback);

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
        if(io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
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
        glEnable(GL_BLEND);

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
        data.flowMap.bind(0);
        data.texture.bind(1);
        
        glUniform1i(       cubeShader.getUniform("u_showFlow"),        data.inputs.showFlow);
        glUniform1i(       cubeShader.getUniform("u_blurPreview"),     data.inputs.blurPreview);
        glUniform1f(       cubeShader.getUniform("u_flowIntensity"),   data.inputs.flowIntensity);
        glUniform1f(       cubeShader.getUniform("u_time"),            glfwGetTime());
        glUniformMatrix4fv(cubeShader.getUniform("u_modelMat"),        1, GL_FALSE, glm::value_ptr(glm::mat4{1.0f}));
        glUniformMatrix4fv(cubeShader.getUniform("u_viewMat"),         1, GL_FALSE, glm::value_ptr(data.viewMat));
        glUniformMatrix4fv(cubeShader.getUniform("u_projectionMat"),   1, GL_FALSE, glm::value_ptr(data.projMat));
        
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
        
        ImGui::Begin(CONFIG_WINDOW_NAME.data(), nullptr);

        ImGui::DragFloat("Sensitivity", &data.inputs.sensitivity, 0.01, 0.01, 100);
        ImGui::SliderFloat("Brush Size", &data.inputs.brushSize, 0.01, 0.5);
        
        static int intflowmapSize = data.flowMapSize;
        if(ImGui::InputInt("Flow Map Size", &intflowmapSize, 100, 1024))
        {
            intflowmapSize = glm::max<int>(intflowmapSize, 100);
            data.flowMapSize = static_cast<unsigned>(intflowmapSize);
            data.flowMap = ogl::Cubemap{0};
            glTextureStorage2D(
                data.flowMap.getRenderID(),
                1,
                GL_RGBA32F,
                data.flowMapSize,
                data.flowMapSize
            );
        }
        ImGui::DragFloat("Flow intensity", &data.inputs.flowIntensity, 0.001, 0.005, 1);

        enum { Mode_Paint, Mode_Erase, ModeCount };
        static int elem = Mode_Paint;
        const char* elems_names[ModeCount] = { "Paint", "Erase" };
        const char* elem_name = (elem >= 0 && elem < ModeCount) ? elems_names[elem] : "Unknown";
        ImGui::SliderInt("Mode", &elem, 0, ModeCount - 1, elem_name); 
        data.inputs.eraseMode = elem == Mode_Erase;

        ImGui::RadioButton("Flow", &data.inputs.showFlow, 1); ImGui::SameLine();
        ImGui::RadioButton("Water", &data.inputs.showFlow, 0);

        ImGui::Checkbox("Blur Preview", &data.inputs.blurPreview);
        helpMarker("Does not affect final result. To blur the output image, use 'Save As... -> Blur steps'.");

        ImGui::Separator();

        if(ImGui::Button("Clear")) 
            ImGui::OpenPopup("Clear?");

        ImGui::SameLine();
        if(ImGui::Button("Load"))
            ImGui::OpenPopup("Load flowmap");

        if(ImGui::Button("Save")) {
            if(data.inputs.path == "")
                ImGui::OpenPopup("Save As");
            else
            {
                switch (data.inputs.saveLayout)
                {
                case UNWRAPPED:
                    saveUnwrapped(data);
                    break;
                case SIX_IMAGES:
                    saveSixImages(data);
                    break;
                case EQUIRECTANGULAR:
                    saveEquirectangular(data);
                    break;
                default:
                    assert(false && "unrecognized save layout (not implemented)");
                    break;
                }
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Save As..."))
            ImGui::OpenPopup("Save As");

        if(ImGui::Button("Load custom texture"))
            ImGui::OpenPopup("Load texture");

        if(!data.messages.empty())
            ImGui::OpenPopup("Message");
        
        if(ImGui::BeginPopupModal("Clear?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("You need to clear after changing hdr option.\nYour current drawing will be cleared.\nThis operation cannot be undone!");
            ImGui::Separator();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::Checkbox("Don't ask me next time", &dontAskToClear);
            ImGui::PopStyleVar();

            if(ImGui::Button("OK", ImVec2(120, 0)) || (dontAskToClear && nextTime)) { 
                clearFlowMap(data);
                nextTime = true;
                ImGui::CloseCurrentPopup(); 
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { 
                ImGui::CloseCurrentPopup(); 
            }
            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("Save As", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Checkbox("HDR flowmap", &data.inputs.hdrFlowmap);
            helpMarker("Flow direction vectors won't be clamped to the range of [0; 1], which allows more dynamic flows.\nThe cube needs to be cleared after changing this option.");
            ImGui::Separator();

            ImGui::Combo("layout", &data.inputs.saveLayout, "unwrapped cube\0six images\0equirectangular\0");
            ImGui::Combo("type", &data.inputs.saveType, "png\0hdr\0");

            static int intHackBlurSteps = data.inputs.blurSteps;
            ImGui::InputInt("Blur steps", &intHackBlurSteps, 1, 10);
            helpMarker("Controls the smoothness of strokes.");
            intHackBlurSteps = glm::max(intHackBlurSteps, 0);
            data.inputs.blurSteps = static_cast<unsigned>(intHackBlurSteps);

            ImGui::Separator();

            if(data.inputs.saveLayout == SIX_IMAGES)
                ImGui::InputText("path (directory)", &data.inputs.path);
            else
                ImGui::InputText("path", &data.inputs.path);

            if(data.inputs.path != "")
            {
                if(ImGui::Button("Save", ImVec2(120, 0))) { 
                    switch (data.inputs.saveLayout)
                    {
                    case UNWRAPPED:
                        saveUnwrapped(data);
                        break;
                    case SIX_IMAGES:
                        saveSixImages(data);
                        break;
                    case EQUIRECTANGULAR:
                        saveEquirectangular(data);
                        break;
                    default:
                        assert(false && "unrecognized save layout");
                        break;
                    }
                    ImGui::CloseCurrentPopup(); 
                }

                ImGui::SameLine();
            }
            ImGui::SetItemDefaultFocus();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup(); 
            }

            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("Load flowmap", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Checkbox("HDR flowmap", &data.inputs.hdrFlowmap);
            ImGui::Separator();

            ImGui::Combo("layout", &data.inputs.saveLayout, "unwrapped cube\0six images\0equirectangular\0");

            ImGui::Separator();

            if(data.inputs.saveLayout == SIX_IMAGES)
                ImGui::InputText("path (directory)", &data.inputs.path);
            else
                ImGui::InputText("path", &data.inputs.path);

            if(data.inputs.path != "")
            {
                if(ImGui::Button("Load", ImVec2(120, 0))) { 
                    MessageStream message;
                    message.setData(data);
                    switch (data.inputs.saveLayout)
                    {
                    case UNWRAPPED:
                        loadUnwrapped(data.inputs.path, data.inputs.hdrFlowmap, data.flowMapSize, message, data.flowMap, true);
                        break;
                    case SIX_IMAGES:
                        loadSixImages(data.inputs.path, data.inputs.hdrFlowmap, data.flowMapSize, message, data.flowMap, true);
                        break;
                    case EQUIRECTANGULAR:
                        loadEquirectangular(data.inputs.path, data.inputs.hdrFlowmap, data.flowMapSize, message, data.flowMap, true);
                        break;
                    default:
                        message << "unrecognized layout!\n";
                        break;
                    }
                    ImGui::CloseCurrentPopup(); 
                }

                ImGui::SameLine();
            }
            ImGui::SetItemDefaultFocus();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { 
                ImGui::CloseCurrentPopup(); 
            }

            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("Message", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(data.messages.front().str().c_str());

            ImGui::Separator();

            if((data.messages.size() == 1) && ImGui::Button("Ok", ImVec2(240, 0)))
            {
                data.messages.pop();
                ImGui::CloseCurrentPopup(); 
            }
            if((data.messages.size() > 1) && ImGui::Button("Next", ImVec2(240, 0)))
            {
                data.messages.pop();
            }

            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("Load texture", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Combo("layout", &data.inputs.textureLayout, "unwrapped cube\0six images\0equirectangular\0one image\0");

            ImGui::Separator();

            if(data.inputs.saveLayout == SIX_IMAGES)
                ImGui::InputText("path (directory)", &data.inputs.texturePath);
            else
                ImGui::InputText("path", &data.inputs.texturePath);

            if(data.inputs.texturePath != "")
            {
                if(ImGui::Button("Load", ImVec2(120, 0))) { 
                    loadCustomTexture(data);
                    ImGui::CloseCurrentPopup(); 
                }

                ImGui::SameLine();
            }
            ImGui::SetItemDefaultFocus();
            if(ImGui::Button("Cancel", ImVec2(120, 0))) { 
                ImGui::CloseCurrentPopup(); 
            }

            ImGui::EndPopup();
        }

        ImGui::End();
        
        glfwPollEvents();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
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

    std::cout << error.id << ": opengl " << error.severity << " severity " << error.type << ", raised from " << error.source << ":\n\t" << error.msg << '\n';
}
void resizeColorAttachment(ogl::Framebuffer &fbo, ogl::TextureMS &texture, glm::ivec2 size, GLenum attachment)
{
    glDeleteTextures(1, &texture.getRenderID());
    texture.getRenderID() = 0;
    glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &texture.getRenderID());
    glTextureStorage2DMultisample(texture.getRenderID(), NUM_SAMPLES, GL_RGBA32F, size.x, size.y, true);
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
    glTextureStorage2D(texture.getRenderID(), 1, GL_RGBA32F, size.x, size.y);
    if(fbo.getRenderID() != 0)
    {
        fbo.attach(texture, GL_COLOR_ATTACHMENT0);
        assert(fbo.isComplete());
    }
}
bool init(GLFWwindow **window)
{
    if(!glfwInit()) {
        std::cout << "failed to initialize glfw!\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, NUM_SAMPLES);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWvidmode const *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    *window = glfwCreateWindow(mode->width * 0.5, mode->height * 0.5, "opengl", nullptr, nullptr);
    glfwSetWindowTitle(*window, "flow cubemap editor v0.5");

    if(!*window) {
        std::cout << "failed to initialize window!\n";
        return false;
    }
    glfwMakeContextCurrent(*window);
    if(!gladLoadGL((GLADloadfunc) glfwGetProcAddress)) {
        std::cout << "gladLoadGL: Failed to initialize GLAD!\n";
        return false;
    }
    
    ImGui::CreateContext();
    IMGUI_CHECKVERSION();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if(getenv("WAYLAND_DISPLAY")) 
        std::cout << "wayland detected! imgui multiple viewports feature is not supported!\n";
    else 
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplGlfw_InitForOpenGL(*window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui::StyleColorsDark();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(debugCallback, nullptr);
    
    glfwSwapInterval(0);

    return true;
}
Mesh loadMesh(std::string_view path)
{
    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = "./";
    tinyobj::ObjReader reader;

    if(!reader.ParseFromFile(std::string{path}, config)) {
        std::cout << "failed to load \"" << path << "\"\n";
        if(!reader.Error().empty()) {
            std::cout << reader.Error() << '\n';
        }
        return Mesh{
            .count = 0
        };
    }

    if(!reader.Warning().empty()) {
        std::cout << reader.Warning().c_str();
    }

    auto &attrib = reader.GetAttrib();
    auto &shapes = reader.GetShapes();
    // auto &materials = reader.GetMaterials();

    std::vector<glm::vec3> positions{};
    std::vector<glm::vec3> normals  {};
    std::vector<glm::vec2> texcoords{};
    std::vector<glm::vec3> tangents {};

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

    assert(positions.size() == normals.size() && positions.size() == texcoords.size());

    // calculate tangents
    tangents.reserve(positions.size());
    for(size_t i = 0; i < positions.size(); i += 3)
    {
        glm::vec3 edge1 = positions[i+1] - positions[i+0];
        glm::vec3 edge2 = positions[i+2] - positions[i+0];
        glm::vec2 deltaUV1 = texcoords[i+1] - texcoords[i+0];
        glm::vec2 deltaUV2 = texcoords[i+2] - texcoords[i+0]; 

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        for(unsigned j = 0; j < 3; ++j)
        {
            tangents.emplace_back(
                f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
                f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
                f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)
            );
        }
    }

    // fill the vbo

    mesh.vbo = ogl::VertexBuffer{
        positions.size() * sizeof(decltype(positions[0])) +
        normals.size()   * sizeof(decltype(normals[0])) +
        texcoords.size() * sizeof(decltype(texcoords[0])) +
        tangents.size() * sizeof(decltype(tangents[0]))
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
            positions.size() * sizeof(decltype(positions[0])) + 
            normals.size()   * sizeof(decltype(normals[0])), 
        texcoords.size() * sizeof(decltype(texcoords[0])),
        texcoords.data()
    );
    glNamedBufferSubData(mesh.vbo.getRenderID(), 
            positions.size() * sizeof(decltype(positions[0])) + 
            normals.size()   * sizeof(decltype(normals[0])) + 
            texcoords.size() * sizeof(decltype(texcoords[0])), 
        tangents.size() * sizeof(decltype(tangents[0])),
        tangents.data()
    );

    ogl::VertexBufferLayout layout = {
        /* 0. posiitons  */ { 3, GL_FLOAT, 0 },
        /* 1. normals    */ { 3, GL_FLOAT, positions.size() * sizeof(decltype(positions[0])) },
        /* 2. tex coords */ { 2, GL_FLOAT, positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0])) },
        /* 3. tangents   */ { 3, GL_FLOAT, positions.size() * sizeof(decltype(positions[0])) + normals.size() * sizeof(decltype(normals[0])) + texcoords.size() * sizeof(decltype(texcoords[0]))} 
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

    if(glm::abs(data.yawPitch.value.y) > 360.0f)
    {
        data.yawPitch.value.y = glm::mod(data.yawPitch.value.y, 360.0f);
    }

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


    float cosTheta = glm::cos(glm::radians(-data.yawPitch.value.x));
    float sinTheta = glm::sin(glm::radians(-data.yawPitch.value.x));
    data.horizontalRotation = glm::mat2{
         cosTheta, sinTheta,
        -sinTheta, cosTheta
    };

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
void drawStroke(Data &data)
{
    glm::vec3 point = unProjectMouse(data);

    glm::vec3 origin = data.cameraPos;
    glm::vec3 dir = glm::normalize(point - origin);
    
    glm::vec2 intersection = rayAABB(origin, dir, glm::vec3{-CUBE_MODEL_SIZE * 0.5f}, glm::vec3{CUBE_MODEL_SIZE * 0.5f});

    if(intersection.x > intersection.y)
        return;
    
    data.intersectionPoint = origin + dir * intersection.x;
    data.intersectionPoint /= CUBE_MODEL_SIZE * 0.5f;
    glm::vec3 prevPoint = data.prevIntersectionPoint == glm::vec3{0} ? data.intersectionPoint : data.prevIntersectionPoint;

    data.flowMapDrawShader.bind();
    glBindImageTexture(0, data.flowMap.getRenderID(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);
    glUniform3fv(data.flowMapDrawShader.getUniform("u_point"),       1, glm::value_ptr(data.intersectionPoint));
    glUniform3fv(data.flowMapDrawShader.getUniform("u_prevPoint"),   1, glm::value_ptr(prevPoint));
    glUniform1f (data.flowMapDrawShader.getUniform("u_brush_size"),     data.inputs.brushSize);
    glUniform1i (data.flowMapDrawShader.getUniform("u_erase"),          data.inputs.eraseMode);
    glUniform1f (data.flowMapDrawShader.getUniform("u_verticalMult"),   glm::abs(data.yawPitch.value.y) > 90.0f ? -1 : 1);
    glUniform2fv(data.flowMapDrawShader.getUniform("u_deltaMouse"),  1, glm::value_ptr(data.deltaMouse / glm::vec2{10.0f})); // FIXME: insert something reasonable here
    glUniformMatrix2fv(data.flowMapDrawShader.getUniform("u_horizontalRotation"), 1, GL_FALSE, glm::value_ptr(data.horizontalRotation));
    glDispatchCompute((data.flowMapSize + 15) / 16, (data.flowMapSize + 7) / 8, 6);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    data.prevIntersectionPoint = data.intersectionPoint;
}
bool shouldProcessInput(Data &data)
{
    return !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
    // return !ImGui::GetIO().WantCaptureMouse;
}
void processInput(Data &data)
{
    assert(data.window);

    bool cameraLocked = glfwGetMouseButton(data.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    glfwSetInputMode(data.window, GLFW_CURSOR, cameraLocked ? GLFW_CURSOR_CAPTURED : GLFW_CURSOR_NORMAL);
    
    
    glfwGetCursorPos(data.window, &data.mousePos.x, &data.mousePos.y);
    data.deltaMouse = data.prevMousePos - data.mousePos;
    data.deltaMouse.x = -data.deltaMouse.x;
    data.deltaMouse /= glm::max<float>(data.windowSize.x, data.windowSize.y) / 1000.0f;
    data.prevMousePos = data.mousePos;

    bool focused = shouldProcessInput(data);

    if((glfwGetKey(data.window, GLFW_KEY_LEFT)  == GLFW_PRESS) && focused) data.yawPitch.velocity.x += data.deltatime * data.inputs.sensitivity * 400.0f;
    if((glfwGetKey(data.window, GLFW_KEY_RIGHT) == GLFW_PRESS) && focused) data.yawPitch.velocity.x -= data.deltatime * data.inputs.sensitivity * 400.0f;
    if((glfwGetKey(data.window, GLFW_KEY_UP)    == GLFW_PRESS) && focused) data.yawPitch.velocity.y += data.deltatime * data.inputs.sensitivity * 400.0f;
    if((glfwGetKey(data.window, GLFW_KEY_DOWN)  == GLFW_PRESS) && focused) data.yawPitch.velocity.y -= data.deltatime * data.inputs.sensitivity * 400.0f;
    if(cameraLocked && focused) 
    {
        data.yawPitch.velocity += glm::vec2{data.deltaMouse.x, -data.deltaMouse.y} * data.deltatime * data.inputs.sensitivity * 400.0f;
    }

    updateVP(data, cameraLocked);
    
    if(glfwGetMouseButton(data.window, GLFW_MOUSE_BUTTON_LEFT) && !cameraLocked && (data.deltaMouse != glm::vec2{0}) && focused)
    {
        drawStroke(data);
    }
    else 
    {
        data.prevIntersectionPoint = glm::vec3{0};
    }
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    Data &data = *static_cast<Data *>(glfwGetWindowUserPointer(window));
    if(shouldProcessInput(data)) {
        data.distance.velocity -= yoffset * data.deltatime * data.inputs.sensitivity * 200.0f;
    } else {
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    }
}
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Data &data = *static_cast<Data *>(glfwGetWindowUserPointer(window));
    if(shouldProcessInput(data)) 
    {
    } else 
    {
        ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    }
}
void helpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if(ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
void clearFlowMap(Data &data)
{
    data.flowMapClearShader.bind();
    glBindImageTexture(0, data.flowMap.getRenderID(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute((data.flowMapSize + 15) / 16, (data.flowMapSize + 7) / 8, 6);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
ogl::Cubemap blurFlowmap(Data &data)
{
    ogl::Cubemap result{0};
    glTextureStorage2D(
        result.getRenderID(),
        1,
        GL_RGBA32F,
        data.flowMapSize,
        data.flowMapSize
    );
    
    glCopyImageSubData(
        data.flowMap.getRenderID(),            // src name
        GL_TEXTURE_CUBE_MAP,                   // src target
        0,                                     // src level
        0, 0, 0,                               // src x,y,z
        result.getRenderID(),                  // dst name
        GL_TEXTURE_CUBE_MAP,                   // dst target
        0,                                     // dst level
        0, 0, 0,                               // dst x,y,z
        data.flowMapSize,                      // width
        data.flowMapSize,                      // height
        6                                      // depth
    );

    data.cubemapBlurShader.bind();
    
    for(unsigned i = 0; i < data.inputs.blurSteps; ++i)
    {
        glBindImageTexture(0, result.getRenderID(),  0, GL_TRUE, 0, GL_READ_ONLY,  GL_RGBA32F);
        glDispatchCompute((data.flowMapSize + 15) / 16, (data.flowMapSize + 7) / 8, 6);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    return result;
}

// 	+----+----+----+
// 	| X+ | Y+ | Z+ |
// 	+----+----+----+
// 	| X- | Y- | Z- |
// 	+----+----+----+
constexpr std::array<glm::uvec2, 6> cells = {
    glm::uvec2{ 0, 0 },
    glm::uvec2{ 0, 1 }, 
    glm::uvec2{ 1, 0 },
    glm::uvec2{ 1, 1 },
    glm::uvec2{ 2, 0 },
    glm::uvec2{ 2, 1 }
};
constexpr std::array<std::string_view, 6> names = {
    "pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z" 
};
constexpr float LDR_SCALE = 10.0f;
using image_ptr = std::unique_ptr<float, decltype(&stbi_image_free)>;
constexpr auto NUM_CUBEMAP_FACES = eqr::NUM_CUBEMAP_FACES;

void save(int type, std::string path, glm::uvec2 size, unsigned numComponents, bool hdr, float *inputData)
{
    if(path == "") 
    {
        std::cout << "path is empty, nothing to save!\n";
        return;
    }

    float *data = inputData;

    if(hdr)
    {
        for (size_t i = 0; i < size.x * size.y * numComponents; i+=4) {
            float &r = data[i+0];
            float &g = data[i+1];
            float &b = data[i+2];
            float &a = data[i+3];
            
            // negative values get clamped to 0
            b = float(r < 0); 
            a = float(g < 0);
            r = glm::abs(r);
            g = glm::abs(g);
        }
    }
    else
    {
        for (size_t i = 0; i < size.x * size.y * numComponents; ++i) {
            data[i] = glm::clamp(data[i] / LDR_SCALE * 0.5f + 0.5f, 0.0f, 1.0f);
        }
        for (size_t i = 0; i < size.x * size.y * numComponents; i+=4) {
            float &b = data[i+2];
            float &a = data[i+3];

            b = 0.0f;
            a = 1.0f;
        }
    }
    

    std::unique_ptr<unsigned char[]> u8data = nullptr;
    if(type != HDR)
    {
        u8data = std::make_unique<unsigned char[]>(size.x * size.y * numComponents);
        for (size_t i = 0; i < size.x * size.y * numComponents; ++i) {
            u8data[i] = static_cast<unsigned char>(glm::clamp(data[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }

    std::replace(path.begin(), path.end(), '\\', '/');
    size_t pos = path.find_last_of('/');
    if(pos != std::string::npos)
        std::filesystem::create_directories(path.substr(0, pos));
    
    switch (type)
    {
    case PNG:
        assert(u8data);
        stbi_write_png(path.data(), size.x, size.y, numComponents, u8data.get(), size.x * numComponents);
        break;
    case HDR:
        stbi_write_hdr(path.data(), size.x, size.y, numComponents, data);
        break;
    default:
        assert(false && "unrecognized save type");
        break;
    }
}
void saveUnwrapped(Data &data)
{
    assert(data.inputs.path != "");
    ogl::Cubemap flowMap = blurFlowmap(data);

    ogl::Texture unwrappedTexture{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE};

    int const numComponents = 4;
    glm::uvec2 const size{data.flowMapSize * 3, data.flowMapSize * 2};

    glTextureStorage2D(unwrappedTexture.getRenderID(), 1, GL_RGBA32F, size.x, size.y);

    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        glm::uvec2 pos = cells[i] * data.flowMapSize;
        glCopyImageSubData(
            flowMap.getRenderID(),          // src name
            GL_TEXTURE_CUBE_MAP,            // src target
            0,                              // src level
            0, 0, i,                        // src x,y,z
            unwrappedTexture.getRenderID(), // dst name
            GL_TEXTURE_2D,                  // dst target
            0,                              // dst level
            pos.x, pos.y, 0,                // dst x,y,z
            data.flowMapSize,               // width
            data.flowMapSize,               // height
            1                               // depth
        );
    }

    std::unique_ptr<float[]> textureData = std::make_unique<float[]>(size.x * size.y * numComponents);
    
    // FIXME: pbo maybe?
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(unwrappedTexture.getRenderID(), 0, GL_RGBA, GL_FLOAT, size.x * size.y * numComponents * sizeof(float), textureData.get());


    save(data.inputs.saveType, data.inputs.path, size, numComponents, data.inputs.hdrFlowmap, textureData.get());
}
void saveSixImages(Data &data)
{
    ogl::Cubemap flowMap = blurFlowmap(data);

    int const numComponents = 4;

    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        ogl::Texture faceTexture{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE};
        glTextureStorage2D(faceTexture.getRenderID(), 1, GL_RGBA32F, data.flowMapSize, data.flowMapSize);

        glCopyImageSubData(
            flowMap.getRenderID(),          // src name
            GL_TEXTURE_CUBE_MAP,            // src target
            0,                              // src level
            0, 0, i,                        // src x,y,z
            faceTexture.getRenderID(),      // dst name
            GL_TEXTURE_2D,                  // dst target
            0,                              // dst level
            0, 0, 0,                        // dst x,y,z
            data.flowMapSize,               // width
            data.flowMapSize,               // height
            1                               // depth
        );

        std::unique_ptr<float[]> textureData = std::make_unique<float[]>(data.flowMapSize * data.flowMapSize * numComponents);
        
        // FIXME: pbo maybe?
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTextureImage(faceTexture.getRenderID(), 0, GL_RGBA, GL_FLOAT, data.flowMapSize * data.flowMapSize * numComponents * sizeof(float), textureData.get());

        std::string path = data.inputs.path;
        path = path + '/' + std::string{names[i]};

        save(data.inputs.saveType, path, glm::uvec2{data.flowMapSize, data.flowMapSize}, numComponents, data.inputs.hdrFlowmap, textureData.get());
    }
}
void saveEquirectangular(Data &data)
{
    ogl::Cubemap flowMap = blurFlowmap(data);

    int const numComponents = 4;

    std::array<Bitmap<float>, NUM_CUBEMAP_FACES> cubemapFaces;

    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        ogl::Texture faceTexture{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE};
        glTextureStorage2D(faceTexture.getRenderID(), 1, GL_RGBA32F, data.flowMapSize, data.flowMapSize);

        glCopyImageSubData(
            flowMap.getRenderID(),          // src name
            GL_TEXTURE_CUBE_MAP,            // src target
            0,                              // src level
            0, 0, i,                        // src x,y,z
            faceTexture.getRenderID(),      // dst name
            GL_TEXTURE_2D,                  // dst target
            0,                              // dst level
            0, 0, 0,                        // dst x,y,z
            data.flowMapSize,               // width
            data.flowMapSize,               // height
            1                               // depth
        );

        cubemapFaces[i] = Bitmap{data.flowMapSize, data.flowMapSize, 4};

        // FIXME: pbo maybe?
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTextureImage(faceTexture.getRenderID(), 0, GL_RGBA, GL_FLOAT, data.flowMapSize * data.flowMapSize * numComponents * sizeof(float), cubemapFaces[i].getData());
    }
    Bitmap<float> equirectangularImage = eqr::fromCubemap(cubemapFaces);

    save(data.inputs.saveType, data.inputs.path, glm::uvec2{equirectangularImage.getWidth(), equirectangularImage.getHeight()}, numComponents, data.inputs.hdrFlowmap, equirectangularImage.getData());
}
template <typename T>
T roundN(T num, unsigned digits = 2)
{
    return glm::round(num * static_cast<T>(glm::pow(10, digits))) / static_cast<T>(glm::pow(10, digits));
}
image_ptr loadImageData(std::string path, bool hdr, bool postProcess, glm::ivec2 &size, std::stringstream &messages)
{
    int numChannels;
    size = glm::ivec2{0};
    stbi_ldr_to_hdr_gamma(1.0f);
    stbi_hdr_to_ldr_gamma(1.0f);
    image_ptr data{stbi_loadf(path.c_str(), &size.x, &size.y, &numChannels, 4), &stbi_image_free}; 
    float *buffer = data.get();
    stbi_ldr_to_hdr_gamma(2.2f);
    stbi_hdr_to_ldr_gamma(2.2f);
    if(!buffer || !size.x || !size.y) 
    {
        messages << "failed to load \"" << path << "\": " << stbi_failure_reason() << '\n';
        return image_ptr{nullptr, &stbi_image_free};
    }

    if(!postProcess)
        return data;
    
    if(hdr)
    {
        for (size_t i = 0; i < static_cast<size_t>(size.x * size.y * 4); i+=4) {
            float &r = buffer[i+0];
            float &g = buffer[i+1];
            float &b = buffer[i+2];
            float &a = buffer[i+3];
            
            // negative values get clamped to 0
            r = b ? -r : r;
            g = a ? -g : g;
        }
    }
    else
    {
        for (size_t i = 0; i < static_cast<size_t>(size.x * size.y * 4); i+=4) {
            float &r = buffer[i+0];
            float &g = buffer[i+1];

            r = roundN(r, 2);
            g = roundN(g, 2);

            r = (r - 0.5f) * 2.0f * LDR_SCALE;
            g = (g - 0.5f) * 2.0f * LDR_SCALE;
        }
    }

    return data;
}
ogl::Texture load(std::string path, bool hdr, bool postProcess, glm::ivec2 &size, std::stringstream &messages)
{
    auto data = loadImageData(path, hdr, postProcess, size, messages);

    if(!data.get())
    {
        return ogl::Texture{};
    }

    ogl::Texture texture{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE};
    texture.bind();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size.x, size.y, 0, GL_RGBA, GL_FLOAT, data.get());

    return texture;
}
bool loadUnwrapped(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess)
{
    glm::ivec2 size;

    ogl::Texture unwrappedTexture = load(path, hdrFlowmap, postProcess, size, message);
    if(unwrappedTexture.getRenderID() == 0) return false;
    
    if(size.x * 2 != size.y * 3)
    {
        message << "wrong layout to load! (expected 3x*2x, got " << size.x << "*" << size.y << ") \"" << path << "\"\n";
        return false;
    }
    
    faceSize = size.y / 2;
    cubemap = ogl::Cubemap{0};
    glTextureStorage2D(
        cubemap.getRenderID(),
        1,
        GL_RGBA32F,
        faceSize,
        faceSize
    );
    
    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        glm::uvec2 pos = cells[i] * faceSize;
        glCopyImageSubData(
            unwrappedTexture.getRenderID(), // src name
            GL_TEXTURE_2D,                  // src target
            0,                              // src level
            pos.x, pos.y, 0,                // src x,y,z
            cubemap.getRenderID(),          // dst name
            GL_TEXTURE_CUBE_MAP,            // dst target
            0,                              // dst level
            0, 0, i,                        // dst x,y,z
            faceSize,                       // width
            faceSize,                       // height
            1                               // depth
        );
    }

    return true;
}
bool loadSixImages(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess)
{
    if(!std::filesystem::exists(path) || !std::filesystem::is_directory(path))
    {
        message << "directory \"" << path << "\" doesent exist!\n";
        return false;
    }
    std::string extension = "";
    for (auto const& dir_entry : std::filesystem::directory_iterator{path}) 
        if(std::find(names.begin(), names.end(), dir_entry.path().stem()) != names.end())
        {
            std::string file_path = dir_entry.path().string();
            extension = file_path.substr(file_path.find_last_of('.'), file_path.size());
            break;
        }
    if(extension == "")
    {
        message << "cant determine file type in directory \"" << path << "\"!\n";
        return false;
    }

    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        std::string file = path;
        file += '/' + std::string{names[i]};
        file += extension;

        glm::ivec2 size;
        auto textureData = loadImageData(file, hdrFlowmap, postProcess, size, message);
        if(!textureData.get()) return false;

        if(size.x != size.y)
        {
            message << "image \"" << file.c_str() << "\" is not square!";
            return false;
        }
        faceSize = size.x;
        if(i == 0)
        {
            cubemap = ogl::Cubemap{0};
            glTextureStorage2D(
                cubemap.getRenderID(),
                1,
                GL_RGBA32F,
                faceSize,
                faceSize
            );
        }
        
        const void* sourceImage = textureData.get();
        glTextureSubImage3D(
            cubemap.getRenderID(), 
            0,       // layer
            0, 0, i, // x,y,z
            faceSize, faceSize, // 2D image dimensions
            1,          // depth
            GL_RGBA,    // format
            GL_FLOAT,   // data type
            sourceImage
        );
    }
    return true;
}
bool loadEquirectangular(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess)
{
    glm::ivec2 size;
    auto imageData = loadImageData(path, hdrFlowmap, postProcess, size, message);
    if(!imageData.get()) return false;

    if(size.x != 2 * size.y)
    {
        message << "image \"" << path.c_str() << "\" is not 2x1!";
        return false;
    }
    faceSize = size.y / 2;

    cubemap = ogl::Cubemap{0};
    glTextureStorage2D(
        cubemap.getRenderID(),
        1,
        GL_RGBA32F,
        faceSize,
        faceSize
    );
    Bitmap<float> equirectangularImage{static_cast<unsigned>(size.x), static_cast<unsigned>(size.y), 4, imageData.get()};

    std::array<Bitmap<float>, NUM_CUBEMAP_FACES> cubemapFaces = eqr::toCubemap(equirectangularImage);

    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        const void* sourceImage = cubemapFaces[i].getData();
        glTextureSubImage3D(
            cubemap.getRenderID(), 
            0,       // layer
            0, 0, i, // x,y,z
            cubemapFaces[0].getWidth(), cubemapFaces[0].getHeight(), // 2D image dimensions
            1,          // depth
            GL_RGBA,    // format
            GL_FLOAT,   // data type
            sourceImage
        );
    }
    return true;
}
bool loadOneImage(std::string path, bool hdrFlowmap, unsigned &faceSize, std::stringstream &message, ogl::Cubemap &cubemap, bool postProcess)
{
    glm::ivec2 size;
    auto textureData = loadImageData(path, hdrFlowmap, postProcess, size, message);
    if(!textureData.get()) return false;

    if(size.x != size.y)
    {
        message << "image \"" << path << "\" is not square!";
        return false;
    }
    faceSize = size.x;
    
    cubemap = ogl::Cubemap{0};
    glTextureStorage2D(
        cubemap.getRenderID(),
        1,
        GL_RGBA32F,
        faceSize,
        faceSize
    );
        
    for(int i = 0; i < NUM_CUBEMAP_FACES; ++i){
        const void* sourceImage = textureData.get();
        glTextureSubImage3D(
            cubemap.getRenderID(), 
            0,       // layer
            0, 0, i, // x,y,z
            faceSize, faceSize, // 2D image dimensions
            1,          // depth
            GL_RGBA,    // format
            GL_FLOAT,   // data type
            sourceImage
        );
    }
    return true;
}
void loadCustomTexture(Data &data)
{
    MessageStream message;
    message.setData(data);
    unsigned faceSize;

    switch (data.inputs.textureLayout)
    {
    case UNWRAPPED:
        {
            loadUnwrapped(data.inputs.texturePath, data.inputs.hdrFlowmap, faceSize, message, data.texture, false);
        }
        break;
    case SIX_IMAGES:
        {
            loadSixImages(data.inputs.texturePath, data.inputs.hdrFlowmap, faceSize, message, data.texture, false);
        }
        break;
    case EQUIRECTANGULAR:
        {
            loadEquirectangular(data.inputs.texturePath, data.inputs.hdrFlowmap, faceSize, message, data.texture, false);
        }
        break;
    case ONE_IMAGE:
        {
            loadOneImage(data.inputs.texturePath, data.inputs.hdrFlowmap, faceSize, message, data.texture, false);
        }
        break;
    default:
        message << "unrecognized layout!\n";
        break;
    }
}
