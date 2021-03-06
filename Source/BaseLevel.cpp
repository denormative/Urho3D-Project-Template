#include "BaseLevel.h"
#include "Input/ControllerInput.h"
#include "SceneManager.h"
#include "Global.h"

BaseLevel::BaseLevel(Context* context) :
Object(context)
{
    SubscribeToBaseEvents();
    scene_ = GetSubsystem<SceneManager>()->GetActiveScene();
    SetGlobalVar("CameraFov", 80);
}

BaseLevel::~BaseLevel()
{
    Dispose();
}

void BaseLevel::SubscribeToBaseEvents()
{
    SubscribeToEvent(MyEvents::E_LEVEL_CHANGING_IN_PROGRESS, URHO3D_HANDLER(BaseLevel, HandleStart));

    // How to use lambda (anonymous) functions
    SendEvent(MyEvents::E_CONSOLE_COMMAND_ADD, MyEvents::ConsoleCommandAdd::P_NAME, "gamma", MyEvents::ConsoleCommandAdd::P_EVENT, "gamma", MyEvents::ConsoleCommandAdd::P_DESCRIPTION, "Change gamma", MyEvents::ConsoleCommandAdd::P_OVERWRITE, true);
    SubscribeToEvent("gamma", [&](StringHash eventType, VariantMap& eventData) {
        StringVector params = eventData["Parameters"].GetStringVector();
        if (params.Size() == 2) {
            float value = ToFloat(params[1]);
            GetSubsystem<ConfigManager>()->Set("engine", "Gamma", value);
            GetSubsystem<ConfigManager>()->Save(true);
            auto* controllerInput = GetSubsystem<ControllerInput>();
            Vector<int> controlIndexes = controllerInput->GetControlIndexes();
            InitViewports(controlIndexes);
        }

        else {
            URHO3D_LOGERROR("Invalid number of parameters");
        }
    });
}

void BaseLevel::HandleStart(StringHash eventType, VariantMap& eventData)
{
    data_ = eventData;
    Init();
    SubscribeToEvents();
}

void BaseLevel::Run()
{
    if (scene_) {
        scene_->SetUpdateEnabled(true);
    }
}

void BaseLevel::Pause()
{
    if (scene_) {
        scene_->SetUpdateEnabled(false);
    }
}

void BaseLevel::SubscribeToEvents()
{
    SubscribeToEvent("FovChange", URHO3D_HANDLER(BaseLevel, HandleFovChange));

    using namespace MyEvents::ConsoleCommandAdd;
    VariantMap data = GetEventDataMap();
    data[P_NAME] = "fov";
    data[P_EVENT] = "FovChange";
    data[P_DESCRIPTION] = "Show/Change camera fov";
    data[P_OVERWRITE] = true;
    SendEvent(MyEvents::E_CONSOLE_COMMAND_ADD, data);
}

void BaseLevel::HandleFovChange(StringHash eventType, VariantMap& eventData)
{
    StringVector params = eventData["Parameters"].GetStringVector();

    if (params.Size() == 1) {
        URHO3D_LOGINFOF("Current fov value: %f", GetGlobalVar("CameraFov").GetFloat());
    }
    else if (params.Size() == 2) {
        float previousValue = GetGlobalVar("CameraFov").GetFloat();
        float value = ToFloat(params.At(1));
        if (value < 60) {
            value = 60;
        }
        if (value > 160) {
            value = 160;
        }
        if (!_cameras.Empty()) {
            for (auto it = _cameras.Begin(); it != _cameras.End(); ++it) {
                Node* cameraNode = (*it).second_;
                cameraNode->GetComponent<Camera>()->SetFov(value);
                SetGlobalVar("CameraFov", value);
            }
        }
        URHO3D_LOGINFOF("Camera fov changed from '%f' to '%f'", previousValue, value);
    }
    else {
        URHO3D_LOGERROR("Invalid number of parameters!");
    }
}

void BaseLevel::Dispose()
{
    // Pause the scene, remove all contents from the scene, then remove the scene itself.
    if (scene_) {
        scene_->SetUpdateEnabled(false);
        scene_->Clear();
        scene_->Remove();
    }

    // Remove all UI elements from UI sub-system
    if (GetSubsystem<UI>()) {
        GetSubsystem<UI>()->GetRoot()->RemoveAllChildren();
    }
}

/**
 * Define rects for splitscreen mode
 */
Vector<IntRect> BaseLevel::InitRects(int count)
{
    auto* graphics = GetSubsystem<Graphics>();
    Vector<IntRect> rects;
    if (count == 1) {
        // whole screen
        rects.Push(IntRect(0, 0, graphics->GetWidth(), graphics->GetHeight()));
    }

        // 2 players - split vertically
    else if (count == 2) {
        rects.Push(IntRect(0, 0, graphics->GetWidth() / 2, graphics->GetHeight()));
        rects.Push(IntRect(graphics->GetWidth() / 2, 0, graphics->GetWidth(), graphics->GetHeight()));
    }

    else if (count == 3) {

        // player 1 - top left corner
        rects.Push(IntRect(0, 0, graphics->GetWidth() / 2, graphics->GetHeight() / 2));
        // player 2 - top right corner
        rects.Push(IntRect(graphics->GetWidth() / 2, 0, graphics->GetWidth(), graphics->GetHeight() / 2));
        // player 3 - bottom
        rects.Push(IntRect(0, graphics->GetHeight() / 2, graphics->GetWidth(), graphics->GetHeight()));
    }
    else if (count == 4) {
        // split screen into 4 rectangles
        rects.Push(IntRect(0, 0, graphics->GetWidth() / 2, graphics->GetHeight() / 2));
        rects.Push(IntRect(graphics->GetWidth() / 2, 0, graphics->GetWidth(), graphics->GetHeight() / 2));
        rects.Push(IntRect(0, graphics->GetHeight() / 2, graphics->GetWidth() / 2, graphics->GetHeight()));
        rects.Push(IntRect(graphics->GetWidth() / 2, graphics->GetHeight() / 2, graphics->GetWidth(), graphics->GetHeight()));
    }

    return rects;
}

/**
 * Create viewports based on controller count
 */
void BaseLevel::InitViewports(Vector<int> playerIndexes)
{
    auto* graphics = GetSubsystem<Graphics>();
    auto* cache = GetSubsystem<ResourceCache>();

    Vector<IntRect> rects = InitRects(playerIndexes.Size());

    Renderer* renderer = GetSubsystem<Renderer>();
    renderer->SetNumViewports(playerIndexes.Size());
    _viewports.Clear();
    _cameras.Clear();

    if (!scene_) {
        return;
    }

    SetGlobalVar("Players", playerIndexes.Size());

    for (unsigned int i = 0; i < playerIndexes.Size(); i++) {
        // Create camera and define viewport. We will be doing load / save, so it's convenient to create the camera outside the scene,
        // so that it won't be destroyed and recreated, and we don't have to redefine the viewport on load
        SharedPtr<Node> cameraNode(scene_->CreateChild("Camera", LOCAL));
        cameraNode->SetPosition(Vector3(0, 1, 0));
        Camera* camera = cameraNode->CreateComponent<Camera>(LOCAL);
        camera->SetFarClip(1000.0f);
        camera->SetNearClip(0.1f);
        camera->SetFov(GetGlobalVar("CameraFov").GetFloat());
        cameraNode->CreateComponent<SoundListener>();
        camera->SetViewMask(1 << i);

        //TODO only the last camera will be the sound listener
        GetSubsystem<Audio>()->SetListener(cameraNode->GetComponent<SoundListener>());
        //GetSubsystem<Audio>()->SetListener(nullptr);

        SharedPtr<Viewport> viewport(new Viewport(context_, scene_, camera, rects[i]));
        SharedPtr<RenderPath> effectRenderPath = viewport->GetRenderPath()->Clone();
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/AutoExposure.xml"));
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/Bloom.xml"));
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/FXAA3.xml"));
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/GammaCorrection.xml"));
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/ColorCorrection.xml"));
        effectRenderPath->Append(cache->GetResource<XMLFile>("PostProcess/Blur.xml"));

        effectRenderPath->SetEnabled("AutoExposure", GetSubsystem<ConfigManager>()->GetBool("engine", "AutoExposure", false));
        effectRenderPath->SetShaderParameter("AutoExposureAdaptRate", GetSubsystem<ConfigManager>()->GetFloat("engine", "AutoExposureAdaptRate", 0.1f));
        effectRenderPath->SetEnabled("Bloom", GetSubsystem<ConfigManager>()->GetBool("engine", "Bloom", false));
        effectRenderPath->SetEnabled("FXAA3", GetSubsystem<ConfigManager>()->GetBool("engine", "FXAA3", true));
        effectRenderPath->SetEnabled("GammaCorrection", GetSubsystem<ConfigManager>()->GetBool("engine", "GammaCorrection", true));
        effectRenderPath->SetEnabled("ColorCorrection", GetSubsystem<ConfigManager>()->GetBool("engine", "ColorCorrection", false));
        float gamma = Clamp(GAMMA_MAX_VALUE - GetSubsystem<ConfigManager>()->GetFloat("engine", "Gamma", 1.0f), 0.05f, GAMMA_MAX_VALUE);
        effectRenderPath->SetShaderParameter("Gamma", gamma);

        effectRenderPath->SetEnabled("Blur", GetSubsystem<ConfigManager>()->GetBool("engine", "Blur", false));
        effectRenderPath->SetShaderParameter("BlurRadius", GetSubsystem<ConfigManager>()->GetFloat("engine", "BlurRadius", 2.0f));
        effectRenderPath->SetShaderParameter("BlurSigma", GetSubsystem<ConfigManager>()->GetFloat("engine", "BlurSigma", 2.0f));

        viewport->SetRenderPath(effectRenderPath);

        Renderer* renderer = GetSubsystem<Renderer>();
        renderer->SetViewport(i, viewport);


        _viewports[playerIndexes[i]] = viewport;
        _cameras[playerIndexes[i]] = cameraNode;
    }

}
