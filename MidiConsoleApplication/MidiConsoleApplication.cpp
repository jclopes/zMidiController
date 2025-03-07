/*
 * This example code looks for the current joystick state once per frame,
 * and draws a visual representation of it.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

 /* Joysticks are low-level interfaces: there's something with a bunch of
    buttons, axes and hats, in no understood order or position. This is
    a flexible interface, but you'll need to build some sort of configuration
    UI to let people tell you what button, etc, does what. On top of this
    interface, SDL offers the "gamepad" API, which works with lots of devices,
    and knows how to map arbitrary buttons and such to look like an
    Xbox/PlayStation/etc gamepad. This is easier, and better, for many games,
    but isn't necessarily a good fit for complex apps and hardware. A flight
    simulator, a realistic racing game, etc, might want this interface instead
    of gamepads. */

    /* SDL can handle multiple joysticks, but for simplicity, this program only
       deals with the first stick it sees. */

#include <string>
#include <vector>
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// RtMidi stuff

#include <RtMidi.h>

// ImGui stuff
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

enum ButtonFunction {
    NOTE,
    CC
};

struct JoystickStatus {
    ButtonFunction func = NOTE;
    int channel = 0; // 0 to 15
    int value = 0;   // 0 to 127
};

/* We will use this renderer to draw into this window every frame. */
static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Joystick* joystick = NULL;

static RtMidiOut* midi_out = NULL;

static std::vector<JoystickStatus> joystick_conf;

const char* button_function_str(ButtonFunction bf) {
    const char* res;
    
    switch (bf) {
    case ButtonFunction::NOTE:
        res = "NOTE";
        break;
    case ButtonFunction::CC:
        res = "CC";
        break;
    default:
        res = NULL;
    }
    return res;
}


const unsigned char button_function_val(ButtonFunction bf, bool release = false) {
    unsigned char res;

    switch (bf) {
    case ButtonFunction::NOTE:
        if (release) {
            res = 0x80;
        }
        else {
            res = 0x90;
        }
        break;
    case ButtonFunction::CC:
        res = 0xB0;
        break;
    default:
        res = 0;
    }
    return res;
}


void joystick_config_ui(SDL_Joystick* joys, std::vector<JoystickStatus>& joy_conf) {
    // TODO: Create a line for each button.
    // button_id; message type [note | cc]; [note | code]
    // Put everything in a table and remove the labels.

    if (joys != NULL) {
        ImGui::SeparatorText("Controller");
        ImGui::Text(SDL_GetJoystickName(joys));
        int button_count = SDL_GetNumJoystickButtons(joys);
        if (ImGui::BeginTable(SDL_GetJoystickName(joys), 4)) {
            ImGui::TableSetupColumn("Bttn");
            ImGui::TableSetupColumn("Func");
            ImGui::TableSetupColumn("Chnl");
            ImGui::TableSetupColumn("Val");
            ImGui::TableHeadersRow();
            for (unsigned int btn = 0; btn < button_count; btn++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", btn);
                ImGui::TableNextColumn();
                ImGui::PushID(btn);
                if (ImGui::BeginCombo("##Func", button_function_str(joy_conf[btn].func), ImGuiComboFlags_None)) {
                    for (unsigned int i = 0; i < 2; i++) {
                        const bool is_selected = (joy_conf[btn].func == i);
                        if (ImGui::Selectable(button_function_str((ButtonFunction)i), is_selected)) {
                            joy_conf[btn].func = (ButtonFunction)i;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                int channel = joy_conf[btn].channel + 1;
                if (ImGui::SliderInt("##Chnl", &channel, 1, 16)) {
                    joy_conf[btn].channel = channel - 1;
                }
                ImGui::TableNextColumn();
                ImGui::SliderInt("##Val", &joy_conf[btn].value, 0, 127);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}


void midi_config_ui(RtMidiOut* mout) {
    static unsigned int selected_port_id = 0;
    std::string selected_port = mout->getPortName(selected_port_id);

    ImGui::SeparatorText("Midi Config");
    // Port DropDown
    if (ImGui::BeginCombo("Port", selected_port.c_str(), ImGuiComboFlags_None)) {
        for (unsigned int i = 0; i < mout->getPortCount(); i++) {
            const bool is_selected = (selected_port_id == i);
            const std::string item = mout->getPortName(i);
            if (ImGui::Selectable(item.c_str(), is_selected)) {
                if (i != selected_port_id) {
                    selected_port_id = i;
                    // TODO: Maybe trigger a SDL_Event and do the port change somewhere else
                    mout->closePort();
                    try {
                        SDL_Log("RtMidi open port %s", mout->getPortName(selected_port_id).c_str());
                        mout->openPort(selected_port_id);
                    }
                    catch (RtMidiError& error) {
                        error.printMessage();
                        // TODO: show the error to user or crash the app.
                    }
                }
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}


/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    int i;

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetAppMetadata("Example Input Joystick Polling", "1.0", "com.example.input-joystick-polling");
 
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("zMIDI Controller", 800, 640, SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
//    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Set ImGui Style
//    ImGui::StyleColorsDark();
    io.FontAllowUserScaling = true;
    io.Fonts->AddFontFromFileTTF("RobotoMono.ttf", 24);

    // Setup Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    try {
        midi_out = new RtMidiOut();
    }
    catch (RtMidiError& error) {
        error.printMessage();
        return SDL_APP_FAILURE;
    }

    // TODO: move the openning of the midi out port to another function
    unsigned int nPorts = midi_out->getPortCount();
    std::cout << "Number of midi ports:" << nPorts << std::endl;
    if (nPorts == 0) {
        std::cout << "No output ports available!" << std::endl;
        return SDL_APP_FAILURE;
    }

    try {
        std::cout << "Openning port: " << midi_out->getPortName(0) << std::endl;
        midi_out->openPort(0);
    }
    catch (RtMidiError &error) {
        error.printMessage();
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    else if (event->type == SDL_EVENT_JOYSTICK_ADDED) {
        /* this event is sent for each hotplugged stick, but also each already-connected joystick during SDL_Init(). */
        if (joystick == NULL) {  /* we don't have a stick yet and one was added, open it! */
            joystick = SDL_OpenJoystick(event->jdevice.which);
            if (!joystick) {
                SDL_Log("Failed to open joystick ID %u: %s", (unsigned int)event->jdevice.which, SDL_GetError());
            }
            int button_count = SDL_GetNumJoystickButtons(joystick);
            for (int i = 0; i < button_count; i++) {
                JoystickStatus js;
                joystick_conf.push_back(js);
            }
        }
    }
    else if (event->type == SDL_EVENT_JOYSTICK_REMOVED) {
        if (joystick && (SDL_GetJoystickID(joystick) == event->jdevice.which)) {
            SDL_CloseJoystick(joystick);  /* our joystick was unplugged. */
            joystick = NULL;
            joystick_conf.clear();
        }
    }
    else if (event->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
        // Get which button was pressed
        int button_id = event->jbutton.button;

        std::vector<unsigned char> message;

        int type_chn = button_function_val(joystick_conf[button_id].func, false) + joystick_conf[button_id].channel;
        message.push_back(type_chn);
        message.push_back(joystick_conf[button_id].value);
        message.push_back(90);
        SDL_Log("Sending message %x %d %d", type_chn, joystick_conf[button_id].value, 90);
        midi_out->sendMessage(&message);
    }
    else if (event->type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
        int button_id = event->jbutton.button;
        int type_chn = button_function_val(joystick_conf[button_id].func, true) + joystick_conf[button_id].channel;
        std::vector<unsigned char> message;
        message.push_back(type_chn);
        message.push_back(joystick_conf[button_id].value);
        //message.push_back(90);
        midi_out->sendMessage(&message);
    }

    ImGui_ImplSDL3_ProcessEvent(event);

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void* appstate)
{
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(800, 640));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    if (ImGui::Begin("UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        midi_config_ui(midi_out);
        joystick_config_ui(joystick, joystick_conf);
    }
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    SDL_RenderClear(renderer);

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    if (joystick) {
        SDL_CloseJoystick(joystick);
    }

    // Cleanup ImGui stuff
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // Cleanup RtMidi stuff
    delete midi_out;

    /* SDL will clean up the window/renderer for us. */
}
