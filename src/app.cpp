#include "app.h"

#ifndef __ANDROID__
#define USE_SDL
#include <GL/glew.h>
#endif

#ifdef USE_SDL
#include <SDL.h>
#endif

#include <filesystem>
#include <thread>

#include "core/log.h"
#include "core/debugrenderer.h"
#include "core/inputbuddy.h"
#include "core/textrenderer.h"
#include "core/xrbuddy.h"

#include "camerasconfig.h"
#include "flycam.h"
#include "gaussiancloud.h"
#include "magiccarpet.h"
#include "pointcloud.h"
#include "pointrenderer.h"
#include "splatrenderer.h"
#include "vrconfig.h"

const float Z_NEAR = 0.1f;
const float Z_FAR = 1000.0f;
const float FOVY = glm::radians(45.0f);

const float MOVE_SPEED = 2.5f;
const float ROT_SPEED = 1.0f;

const glm::vec4 WHITE = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
const glm::vec4 BLACK = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
const int TEXT_NUM_ROWS = 25;

#include <string>
#include <filesystem>
#include <iostream>

// searches for file named configFilename, dir that contains plyFilename, it's parent and grandparent dirs.
static std::string FindConfigFile(const std::string& plyFilename, const std::string& configFilename)
{
    std::filesystem::path plyPath(plyFilename);

    if (!std::filesystem::exists(plyPath) || !std::filesystem::is_regular_file(plyPath))
    {
        Log::E("PLY file does not exist or is not a file: \"%s\"", plyFilename.c_str());
        return "";
    }

    std::filesystem::path directory = plyPath.parent_path();

    for (int i = 0; i < 3; ++i) // Check current, parent, and grandparent directories
    {
        std::filesystem::path configPath = directory / configFilename;
        if (std::filesystem::exists(configPath) && std::filesystem::is_regular_file(configPath))
        {
            return configPath.string();
        }
        if (directory.has_parent_path())
        {
            directory = directory.parent_path();
        }
        else
        {
            break;
        }
    }

    return "";
}

std::string GetFilenameWithoutExtension(const std::string& filepath)
{
    std::filesystem::path pathObj(filepath);

    // Check if the path has a stem (the part of the path before the extension)
    if(pathObj.has_stem())
    {
        return pathObj.stem().string();
    }

    // If there is no stem, return an empty string
    return "";
}

static void Clear(glm::ivec2 windowSize, bool setViewport = true)
{
    int width = windowSize.x;
    int height = windowSize.y;
    if (setViewport)
    {
        glViewport(0, 0, width, height);
    }

    // pre-multiplied alpha blending
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glm::vec4 clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);

#ifndef __ANDROID__
    // AJT: ANDROID: TODO: implement this in fragment shader, for OpenGLES I guess.
    // enable alpha test
    //glEnable(GL_ALPHA_TEST);
    //glAlphaFunc(GL_GREATER, 1.0f / 256.0f);
#endif
}

// Draw a textured quad over the entire screen.
static void RenderDesktop(glm::ivec2 windowSize, std::shared_ptr<Program> desktopProgram, uint32_t colorTexture)
{
    int width = windowSize.x;
    int height = windowSize.y;

    glViewport(0, 0, width, height);
    glm::vec4 clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 projMat = glm::ortho(0.0f, (float)width, 0.0f, (float)height, -10.0f, 10.0f);

    if (colorTexture > 0)
    {
        desktopProgram->Bind();
        desktopProgram->SetUniform("modelViewProjMat", projMat);
        desktopProgram->SetUniform("color", glm::vec4(1.0f));

        // use texture unit 0 for colorTexture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        desktopProgram->SetUniform("colorTexture", 0);

        glm::vec2 xyLowerLeft(0.0f, (height - width) / 2.0f);
        glm::vec2 xyUpperRight((float)width, (height + width) / 2.0f);
        glm::vec2 uvLowerLeft(0.0f, 0.0f);
        glm::vec2 uvUpperRight(1.0f, 1.0f);

        float depth = -9.0f;
        glm::vec3 positions[] = {glm::vec3(xyLowerLeft, depth), glm::vec3(xyUpperRight.x, xyLowerLeft.y, depth),
                                 glm::vec3(xyUpperRight, depth), glm::vec3(xyLowerLeft.x, xyUpperRight.y, depth)};
        desktopProgram->SetAttrib("position", positions);

        glm::vec2 uvs[] = {uvLowerLeft, glm::vec2(uvUpperRight.x, uvLowerLeft.y),
                           uvUpperRight, glm::vec2(uvLowerLeft.x, uvUpperRight.y)};
        desktopProgram->SetAttrib("uv", uvs);

        const size_t NUM_INDICES = 6;
        uint16_t indices[NUM_INDICES] = {0, 1, 2, 0, 2, 3};
        glDrawElements(GL_TRIANGLES, NUM_INDICES, GL_UNSIGNED_SHORT, indices);
    }
}

// AJT: TODO this wrapper func is not needed anymore
static std::shared_ptr<PointCloud> LoadPointCloud(const std::string& plyFilename)
{
    auto pointCloud = std::make_shared<PointCloud>();

    if (!pointCloud->ImportPly(plyFilename))
    {
        Log::E("Error loading PointCloud!\n");
        return nullptr;
    }
    return pointCloud;
}

// AJT: TODO this wrapper func is not needed anymore
static std::shared_ptr<GaussianCloud> LoadGaussianCloud(const std::string& plyFilename)
{
    auto gaussianCloud = std::make_shared<GaussianCloud>();

    if (!gaussianCloud->ImportPly(plyFilename))
    {
        Log::E("Error loading GaussianCloud!\n");
        return nullptr;
    }

    return gaussianCloud;
}

App::App(const MainContext& mainContextIn)
{
    mainContext = mainContextIn;
    cameraIndex = 0;
    virtualLeftStick = glm::vec2(0.0f, 0.0f);
    virtualRightStick = glm::vec2(0.0f, 0.0f);
    virtualRoll = 0.0f;
}

bool App::ParseArguments(int argc, const char* argv[])
{
    // parse arguments
    if (argc < 2)
    {
        Log::E("Missing FILENAME argument\n");
        // AJT: TODO print usage
        return false;
    }
    else
    {
        for (int i = 1; i < (argc - 1); i++)
        {
            if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--openxr"))
            {
                opt.vrMode = true;
            }
            else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--fullscreen"))
            {
                opt.fullscreen = true;
            }
            else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug"))
            {
                opt.debugLogging = true;
            }
        }
        plyFilename = argv[argc - 1];
    }

    Log::SetLevel(opt.debugLogging ? Log::Debug : Log::Warning);

    std::filesystem::path plyPath(plyFilename);
    if (!std::filesystem::exists(plyPath) || !std::filesystem::is_regular_file(plyPath))
    {
        Log::E("Invalid file \"%s\"\n", plyFilename.c_str());
        return false;
    }

    return true;
}

bool App::Init()
{
    bool isFramebufferSRGBEnabled = opt.vrMode;

#ifndef __ANDROID__
    // AJT: ANDROID: TODO: make sure colors are accurate on android.
    if (isFramebufferSRGBEnabled)
    {
        // necessary for proper color conversion
        glEnable(GL_FRAMEBUFFER_SRGB);
    }
    else
    {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        Log::E("Error: %s\n", glewGetErrorString(err));
        return false;
    }
#endif

    debugRenderer = std::make_shared<DebugRenderer>();
    if (!debugRenderer->Init())
    {
        Log::E("DebugRenderer Init failed\n");
        return false;
    }

    textRenderer = std::make_shared<TextRenderer>();
    if (!textRenderer->Init("font/JetBrainsMono-Medium.json", "font/JetBrainsMono-Medium.png"))
    {
        Log::E("TextRenderer Init failed\n");
        return false;
    }

    if (opt.vrMode)
    {
        xrBuddy = std::make_shared<XrBuddy>(mainContext, glm::vec2(Z_NEAR, Z_FAR));
        if (!xrBuddy->Init())
        {
            Log::E("OpenXR Init failed\n");
            return false;
        }
    }

    std::string camerasConfigFilename = FindConfigFile(plyFilename, "cameras.json");
    if (!camerasConfigFilename.empty())
    {
        camerasConfig = std::make_shared<CamerasConfig>();
        if (!camerasConfig->ImportJson(camerasConfigFilename))
        {
            Log::W("Error loading cameras.json\n");
            camerasConfig.reset();
        }
    }
    else
    {
        Log::D("Could not find cameras.json\n");
    }

    // search for vr config file
    // for example: if plyFilename is "input.ply", then search for "input_vr.json"
    std::string vrConfigFilename = FindConfigFile(plyFilename, GetFilenameWithoutExtension(plyFilename) + "_vr.json");
    if (!vrConfigFilename.empty())
    {
        vrConfig = std::make_shared<VrConfig>();
        if (!vrConfig->ImportJson(vrConfigFilename))
        {
            Log::I("Could not load vr.json\n");
            vrConfig.reset();
        }
    }
    else
    {
        Log::D("Could not find vr.json\n");
    }

    flyCam = std::make_shared<FlyCam>(glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), MOVE_SPEED, ROT_SPEED);
    glm::mat4 floorMat(1.0f);
    cameraIndex = 0;
    if (camerasConfig)
    {
        flyCam->SetCameraMat(camerasConfig->GetCameraVec()[cameraIndex]);

        // initialize magicCarpet from first camera and estimated floor position.
        if (camerasConfig->GetNumCameras() > 0)
        {
            glm::vec3 floorNormal, floorPos;
            camerasConfig->EstimateFloorPlane(floorNormal, floorPos);
            glm::vec3 floorZ = camerasConfig->GetCameraVec()[0][2];
            glm::vec3 floorY = floorNormal;
            glm::vec3 floorX = glm::cross(floorY, floorZ);
            floorZ = glm::cross(floorX, floorY);

            floorMat = glm::mat4(glm::vec4(floorX, 0.0f),
                                 glm::vec4(floorY, 0.0f),
                                 glm::vec4(floorZ, 0.0f),
                                 glm::vec4(floorPos, 1.0f));
        }
    }

    if (vrConfig)
    {
        floorMat = vrConfig->GetFloorMat();

        if (!camerasConfig)
        {
            glm::vec3 pos = floorMat[3];
            pos += glm::mat3(floorMat) * glm::vec3(0.0f, 1.5f, 0.0f);
            glm::mat4 adjustedFloorMat = floorMat;
            adjustedFloorMat[3] = glm::vec4(pos, 1.0f);
            flyCam->SetCameraMat(adjustedFloorMat);
        }
    }

    magicCarpet = std::make_shared<MagicCarpet>(floorMat, MOVE_SPEED);
    if (!magicCarpet->Init(isFramebufferSRGBEnabled))
    {
        Log::E("Error initalizing MagicCarpet\n");
        return false;
    }

    std::string pointCloudFilename = FindConfigFile(plyFilename, "input.ply");
    if (!pointCloudFilename.empty())
    {
        pointCloud = LoadPointCloud(pointCloudFilename);
        if (!pointCloud)
        {
            Log::E("Error loading PointCloud\n");
            return false;
        }

        pointRenderer = std::make_shared<PointRenderer>();
        if (!pointRenderer->Init(pointCloud, isFramebufferSRGBEnabled))
        {
            Log::E("Error initializing point renderer!\n");
            return false;
        }
    }
    else
    {
        Log::D("Could not find input.ply\n");
    }

    gaussianCloud = LoadGaussianCloud(plyFilename);
    if (!gaussianCloud)
    {
        Log::E("Error loading GaussianCloud\n");
        return false;
    }

#if 0
    const uint32_t SPLAT_COUNT = 25000;
    glm::vec3 focalPoint = flyCam->GetCameraMat()[3];
    //gaussianCloud->PruneSplats(glm::vec3(flyCam->GetCameraMat()[3]), SPLAT_COUNT);
    gaussianCloud->PruneSplats(focalPoint, SPLAT_COUNT);
#endif

    splatRenderer = std::make_shared<SplatRenderer>();
#if __ANDROID__
    bool useFullSH = false;
#else
    bool useFullSH = true;
#endif
    if (!splatRenderer->Init(gaussianCloud, isFramebufferSRGBEnabled, useFullSH))
    {
        Log::E("Error initializing splat renderer!\n");
        return false;
    }

    if (opt.vrMode)
    {
        // TODO: move this into a DesktopRenderer class
        desktopProgram = std::make_shared<Program>();
        std::string defines = "#define USE_SUPERSAMPLING\n";
        desktopProgram->AddMacro("DEFINES", defines);
        if (!desktopProgram->LoadVertFrag("shader/desktop_vert.glsl", "shader/desktop_frag.glsl"))
        {
            Log::E("Error loading desktop shader!\n");
            return 1;
        }

        xrBuddy->SetRenderCallback([this](
            const glm::mat4& projMat, const glm::mat4& eyeMat,
            const glm::vec4& viewport, const glm::vec2& nearFar, int viewNum)
        {
            Clear(glm::ivec2(0, 0), false);

            glm::mat4 fullEyeMat = magicCarpet->GetCarpetMat() * eyeMat;

            if (opt.drawDebug)
            {
                debugRenderer->Render(fullEyeMat, projMat, viewport, nearFar);
            }

            if (opt.drawCarpet)
            {
                magicCarpet->Render(fullEyeMat, projMat, viewport, nearFar);
            }

            if (opt.drawPointCloud && pointRenderer)
            {
                pointRenderer->Render(fullEyeMat, projMat, viewport, nearFar);
            }
            else
            {
                if (viewNum == 0)
                {
                    splatRenderer->Sort(fullEyeMat, projMat, viewport, nearFar);
                }
                splatRenderer->Render(fullEyeMat, projMat, viewport, nearFar);
            }
        });
    }

#ifdef USE_SDL
    inputBuddy = std::make_shared<InputBuddy>();

    inputBuddy->OnQuit([this]()
    {
        // forward this back to main
        quitCallback();
    });

    inputBuddy->OnResize([this](int newWidth, int newHeight)
    {
        glViewport(0, 0, newWidth, newHeight);
        resizeCallback(newWidth, newHeight);
    });

    inputBuddy->OnKey(SDLK_ESCAPE, [this](bool down, uint16_t mod)
    {
        quitCallback();
    });

    inputBuddy->OnKey(SDLK_c, [this](bool down, uint16_t mod)
    {
        if (down)
        {
            opt.drawPointCloud = !opt.drawPointCloud;
        }
    });

    inputBuddy->OnKey(SDLK_n, [this](bool down, uint16_t mod)
    {
        if (down && camerasConfig)
        {
            cameraIndex = (cameraIndex + 1) % camerasConfig->GetNumCameras();
            flyCam->SetCameraMat(camerasConfig->GetCameraVec()[cameraIndex]);
        }
    });

    inputBuddy->OnKey(SDLK_p, [this](bool down, uint16_t mod)
    {
        if (down && camerasConfig)
        {
            cameraIndex = (cameraIndex - 1) % camerasConfig->GetNumCameras();
            flyCam->SetCameraMat(camerasConfig->GetCameraVec()[cameraIndex]);
        }
    });

    inputBuddy->OnKey(SDLK_f, [this](bool down, uint16_t mod)
    {
        if (down)
        {
            opt.drawCarpet = !opt.drawCarpet;
        }
    });

    inputBuddy->OnKey(SDLK_RETURN, [this, vrConfigFilename](bool down, uint16_t mod)
    {
        if (down && opt.vrMode && vrConfig)
        {
            vrConfig->SetFloorMat(magicCarpet->GetCarpetMat());
            if (vrConfig->ExportJson(vrConfigFilename))
            {
                Log::I("Wrote \"%s\"\n", vrConfigFilename.c_str());
            }
            else
            {
                Log::E("Writing \"%s\" failed\n", vrConfigFilename.c_str());
            }
        }
    });

    inputBuddy->OnKey(SDLK_F1, [this](bool down, uint16_t mod)
    {
        if (down)
        {
            opt.drawFps = !opt.drawFps;
        }
    });

    inputBuddy->OnKey(SDLK_a, [this](bool down, uint16_t mod)
    {
        virtualLeftStick.x += down ? -1.0f : 1.0f;
    });

    inputBuddy->OnKey(SDLK_d, [this](bool down, uint16_t mod)
    {
        virtualLeftStick.x += down ? 1.0f : -1.0f;
    });

    inputBuddy->OnKey(SDLK_w, [this](bool down, uint16_t mod)
    {
        virtualLeftStick.y += down ? 1.0f : -1.0f;
    });

    inputBuddy->OnKey(SDLK_s, [this](bool down, uint16_t mod)
    {
        virtualLeftStick.y += down ? -1.0f : 1.0f;
    });

    inputBuddy->OnKey(SDLK_LEFT, [this](bool down, uint16_t mod)
    {
        virtualRightStick.x += down ? -1.0f : 1.0f;
    });

    inputBuddy->OnKey(SDLK_RIGHT, [this](bool down, uint16_t mod)
    {
        virtualRightStick.x += down ? 1.0f : -1.0f;
    });

    inputBuddy->OnKey(SDLK_UP, [this](bool down, uint16_t mod)
    {
        virtualRightStick.y += down ? 1.0f : -1.0f;
    });

    inputBuddy->OnKey(SDLK_DOWN, [this](bool down, uint16_t mod)
    {
        virtualRightStick.y += down ? -1.0f : 1.0f;
    });

    inputBuddy->OnKey(SDLK_q, [this](bool down, uint16_t mod)
    {
        virtualRoll += down ? -1.0f : 1.0f;
    });

    inputBuddy->OnKey(SDLK_e, [this](bool down, uint16_t mod)
    {
        virtualRoll += down ? 1.0f : -1.0f;
    });
#endif // USE_SDL

    fpsText = textRenderer->AddScreenTextWithDropShadow(glm::ivec2(0, 0), (int)TEXT_NUM_ROWS, WHITE, BLACK, "fps:");

    return true;
}

void App::ProcessEvent(const SDL_Event& event)
{
#ifdef USE_SDL
    inputBuddy->ProcessEvent(event);
#endif
}

void App::UpdateFps(float fps)
{
    std::string text = "fps: " + std::to_string((int)fps);
    textRenderer->RemoveText(fpsText);
    fpsText = textRenderer->AddScreenTextWithDropShadow(glm::ivec2(0, 0), TEXT_NUM_ROWS, WHITE, BLACK, text);
}

bool App::Process(float dt)
{
    if (opt.vrMode)
    {
        if (!xrBuddy->PollEvents())
        {
            Log::E("xrBuddy PollEvents failed\n");
            return false;
        }

        if (!xrBuddy->SyncInput())
        {
            Log::E("xrBuddy SyncInput failed\n");
            return false;
        }

        // copy vr input into MagicCarpet
        MagicCarpet::Pose headPose, rightPose, leftPose;
        if (!xrBuddy->GetActionPosition("head_pose", &headPose.pos, &headPose.posValid, &headPose.posTracked))
        {
            Log::W("xrBuddy GetActionPosition(head_pose) failed\n");
        }
        if (!xrBuddy->GetActionOrientation("head_pose", &headPose.rot, &headPose.rotValid, &headPose.rotTracked))
        {
            Log::W("xrBuddy GetActionOrientation(head_pose) failed\n");
        }
        xrBuddy->GetActionPosition("l_aim_pose", &leftPose.pos, &leftPose.posValid, &leftPose.posTracked);
        xrBuddy->GetActionOrientation("l_aim_pose", &leftPose.rot, &leftPose.rotValid, &leftPose.rotTracked);
        xrBuddy->GetActionPosition("r_aim_pose", &rightPose.pos, &rightPose.posValid, &rightPose.posTracked);
        xrBuddy->GetActionOrientation("r_aim_pose", &rightPose.rot, &rightPose.rotValid, &rightPose.rotTracked);
        glm::vec2 leftStick, rightStick;
        bool valid, changed;
        xrBuddy->GetActionVec2("l_stick", &leftStick, &valid, &changed);
        xrBuddy->GetActionVec2("r_stick", &rightStick, &valid, &changed);
        MagicCarpet::ButtonState buttonState;
        xrBuddy->GetActionBool("l_select_click", &buttonState.leftTrigger, &valid, &changed);
        xrBuddy->GetActionBool("r_select_click", &buttonState.rightTrigger, &valid, &changed);
        xrBuddy->GetActionBool("l_squeeze_click", &buttonState.leftGrip, &valid, &changed);
        xrBuddy->GetActionBool("r_squeeze_click", &buttonState.rightGrip, &valid, &changed);
        magicCarpet->Process(headPose, leftPose, rightPose, leftStick, rightStick, buttonState, dt);
    }
#ifdef USE_SDL
    InputBuddy::Joypad joypad = inputBuddy->GetJoypad();
    float roll = 0.0f;
    roll -= joypad.lb ? 1.0f : 0.0f;
    roll += joypad.rb ? 1.0f : 0.0f;
    flyCam->Process(joypad.leftStick + virtualLeftStick,
                    joypad.rightStick + virtualRightStick,
                    roll + virtualRoll, dt);
#endif

    return true;
}

bool App::Render(float dt, const glm::ivec2& windowSize)
{
    int width = windowSize.x;
    int height = windowSize.y;

    if (opt.vrMode)
    {
        if (xrBuddy->SessionReady())
        {
            if (!xrBuddy->RenderFrame())
            {
                Log::E("xrBuddy RenderFrame failed\n");
                return false;
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#ifndef __ANDROID__
        // render desktop.
        Clear(windowSize, true);
        RenderDesktop(windowSize, desktopProgram, xrBuddy->GetColorTexture());

        if (opt.drawFps)
        {
            glm::vec4 viewport(0.0f, 0.0f, (float)width, (float)height);
            glm::vec2 nearFar(Z_NEAR, Z_FAR);
            glm::mat4 projMat = glm::perspective(FOVY, (float)width / (float)height, Z_NEAR, Z_FAR);
            textRenderer->Render(glm::mat4(1.0f), projMat, viewport, nearFar);
        }
#endif
    }
    else
    {
        Clear(windowSize, true);

        glm::mat4 cameraMat = flyCam->GetCameraMat();
        glm::vec4 viewport(0.0f, 0.0f, (float)width, (float)height);
        glm::vec2 nearFar(Z_NEAR, Z_FAR);
        glm::mat4 projMat = glm::perspective(FOVY, (float)width / (float)height, Z_NEAR, Z_FAR);

        if (opt.drawDebug)
        {
            debugRenderer->Render(cameraMat, projMat, viewport, nearFar);
        }

        if (opt.drawCarpet)
        {
            magicCarpet->Render(cameraMat, projMat, viewport, nearFar);
        }

        if (opt.drawPointCloud && pointRenderer)
        {
            pointRenderer->Render(cameraMat, projMat, viewport, nearFar);
        }
        else
        {
            splatRenderer->Sort(cameraMat, projMat, viewport, nearFar);
            splatRenderer->Render(cameraMat, projMat, viewport, nearFar);
        }

        if (opt.drawFps)
        {
            textRenderer->Render(cameraMat, projMat, viewport, nearFar);
        }
    }

    debugRenderer->EndFrame();

    return true;
}

void App::OnQuit(const VoidCallback& cb)
{
    quitCallback = cb;
}

void App::OnResize(const ResizeCallback& cb)
{
    resizeCallback = cb;
}
