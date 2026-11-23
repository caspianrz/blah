#include "blah_platform.h"

#include <blah_app.h>
#include <blah_common.h>
#include <blah_filesystem.h>
#include <blah_input.h>
#include <blah_time.h>

#include <SDL3/SDL.h>

// for File Reading / Writing
#include <filesystem>

// Windows requires a few extra includes
#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <SDL_syswm.h> // for SDL_SysWMinfo for D3D11
#include <shellapi.h>  // for ShellExecute for dir_explore
#include <windows.h>   // for the following includes
#endif

// Macro defined by X11 conflicts with MouseButton enum
#undef None

namespace Blah {
	void blah_sdl_log(void *userdata, int category, SDL_LogPriority priority,
					  const char *message) {
		if (priority <= SDL_LOG_PRIORITY_INFO)
			Log::info(message);
		else if (priority <= SDL_LOG_PRIORITY_WARN)
			Log::warn(message);
		else
			Log::error(message);
	}

	int blah_sdl_find_joystick_index(SDL_Joystick **joysticks,
									 SDL_JoystickID instance_id) {
		for (int i = 0; i < Input::max_controllers; i++)
			if (joysticks[i] != nullptr &&
				SDL_GetJoystickID(joysticks[i]) == instance_id)
				return i;
		return -1;
	}

	int blah_sdl_find_gamepad_index(SDL_Gamepad	 **gamepads,
									SDL_JoystickID instance_id) {
		for (int i = 0; i < Input::max_controllers; i++) {
			if (gamepads[i] != nullptr) {
				auto joystick = SDL_GetGamepadJoystick(gamepads[i]);
				if (SDL_GetJoystickID(joystick) == instance_id)
					return i;
			}
		}
		return -1;
	}

	struct SDL3_File : public File {
		SDL_IOStream *handle;

		SDL3_File(SDL_IOStream *handle) : handle(handle) {
		}

		~SDL3_File() {
			if (handle)
				SDL_CloseIO(handle);
		}

		size_t length() override {
			return static_cast<size_t>(SDL_GetIOSize(handle));
		}

		size_t position() override {
			return static_cast<size_t>(SDL_TellIO(handle));
		}

		size_t seek(size_t position) override {
			return static_cast<size_t>(SDL_SeekIO(
				handle, static_cast<Sint64>(position), SDL_IO_SEEK_SET));
		}

		size_t read(void *buffer, size_t length) override {
			return SDL_ReadIO(handle, buffer, length);
		}

		size_t write(const void *buffer, size_t length) override {
			return SDL_WriteIO(handle, buffer, length);
		}
	};

	SDL_Window	 *sdl3_window = nullptr;
	SDL_Joystick *sdl3_joysticks[Input::max_controllers];
	SDL_Gamepad	 *sdl3_gamepads[Input::max_controllers];

	char *sdl3_base_path_value = nullptr;
	char *sdl3_user_path_value = nullptr;
	char *sdl3_clipboard_text  = nullptr;

	bool sdl3_displayed = false;
} // namespace Blah

using namespace Blah;

bool Platform::init(const Config &config) {
	// TODO:
	// control this via some kind of config flag
#ifndef __EMSCRIPTEN__
	// Note: Emscripten gets a Stack Overflow if this is assigned to Verbose,
	// even when increasing the node stack size to +8mb. Some kind of SDL2
	// problem?
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#endif
	SDL_SetLogOutputFunction(blah_sdl_log, nullptr);

	// Get SDL version
	int version = SDL_GetVersion();

	Log::info("SDL v%i.%i.%i", SDL_VERSIONNUM_MAJOR(version),
			  SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version));

	// Make us DPI aware on Windows
	// SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
	// SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");

	// initialize SDL
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		Log::error("Failed to initialize SDL3: %s", SDL_GetError());
		return false;
	}

	int flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;

	// enable OpenGL
	if (config.renderer_type == RendererType::OpenGL) {
		flags |= SDL_WINDOW_OPENGL;

#ifdef __EMSCRIPTEN__
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
							SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
							SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		// TODO:
		// This should be controlled via the gfx api somehow?
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif
	}

	// create the window
	sdl3_window =
		SDL_CreateWindow(config.name, config.width, config.height, flags);

	if (sdl3_window == nullptr) {
		Log::error("Failed to create a Window");
		return false;
	} else {
		SDL_SetWindowPosition(sdl3_window, SDL_WINDOWPOS_CENTERED,
							  SDL_WINDOWPOS_CENTERED);
	}

	for (int i = 0; i < Input::max_controllers; i++) {
		sdl3_joysticks[i] = nullptr;
		sdl3_gamepads[i]  = nullptr;
	}

	return true;
}

void Platform::ready() {
}

void Platform::shutdown() {
	if (sdl3_window != nullptr)
		SDL_DestroyWindow(sdl3_window);
	sdl3_window	   = nullptr;
	sdl3_displayed = false;

	if (sdl3_base_path_value != nullptr)
		SDL_free(sdl3_base_path_value);

	if (sdl3_user_path_value != nullptr)
		SDL_free(sdl3_user_path_value);

	if (sdl3_clipboard_text != nullptr)
		SDL_free(sdl3_clipboard_text);

	SDL_Quit();
}

u64 Platform::ticks() {
	auto counter	= SDL_GetPerformanceCounter();
	auto per_second = (double) SDL_GetPerformanceFrequency();
	return (u64) (counter * (Time::ticks_per_second / per_second));
}

void Platform::update(InputState &state) {
	// update the mouse every frame
	{
		float x, y;
#ifdef __EMSCRIPTEN__
		// Browsers expose canvas-relative coordinates, not desktop positions.
		SDL_GetMouseState(&x, &y);
		state.mouse.on_move(Vec2f(x, y), Vec2f(x, y));
#else
		int win_x, win_y;

		SDL_GetWindowPosition(sdl3_window, &win_x, &win_y);
		SDL_GetGlobalMouseState(&x, &y);

		state.mouse.on_move(Vec2f((float) (x - win_x), (float) (y - win_y)),
							Vec2f((float) x, (float) y));
#endif
	}

	// poll normal events
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_QUIT) {
			auto &config = App::config();
			if (config.on_exit_request != nullptr)
				config.on_exit_request();
		}
		// Mouse
		else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			MouseButton btn = MouseButton::None;
			if (event.button.button == SDL_BUTTON_LEFT)
				btn = MouseButton::Left;
			else if (event.button.button == SDL_BUTTON_RIGHT)
				btn = MouseButton::Right;
			else if (event.button.button == SDL_BUTTON_MIDDLE)
				btn = MouseButton::Middle;

			state.mouse.on_press(btn);
		} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
			MouseButton btn = MouseButton::None;
			if (event.button.button == SDL_BUTTON_LEFT)
				btn = MouseButton::Left;
			else if (event.button.button == SDL_BUTTON_RIGHT)
				btn = MouseButton::Right;
			else if (event.button.button == SDL_BUTTON_MIDDLE)
				btn = MouseButton::Middle;

			state.mouse.on_release(btn);
		} else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
			state.mouse.wheel = Point(event.wheel.x, event.wheel.y);
		}
		// Keyboard
		else if (event.type == SDL_EVENT_KEY_DOWN) {
			if (!event.key.repeat)
				state.keyboard.on_press((Key) event.key.scancode);
		} else if (event.type == SDL_EVENT_KEY_UP) {
			if (!event.key.repeat)
				state.keyboard.on_release((Key) event.key.scancode);
		} else if (event.type == SDL_EVENT_TEXT_INPUT) {
			state.keyboard.text += event.text.text;
		}
		// Joystick Controller
		else if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
			auto index = event.jdevice.which;

			if (SDL_IsGamepad(index) == false && index >= 0 &&
				index < Input::max_controllers) {
				auto ptr = sdl3_joysticks[index] = SDL_OpenJoystick(index);
				auto name						 = SDL_GetJoystickName(ptr);
				auto button_count = SDL_GetNumJoystickButtons(ptr);
				auto axis_count	  = SDL_GetNumJoystickAxes(ptr);
				auto vendor		  = SDL_GetJoystickVendor(ptr);
				auto product	  = SDL_GetJoystickProduct(ptr);
				auto version	  = SDL_GetJoystickProductVersion(ptr);

				state.controllers[index].on_connect(name, 0, button_count,
													axis_count, vendor, product,
													version);
			}
		} else if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
			auto index = blah_sdl_find_joystick_index(sdl3_joysticks,
													  event.jdevice.which);
			if (index >= 0) {
				if (SDL_IsGamepad(index) == false) {
					state.controllers[index].on_disconnect();
					SDL_CloseJoystick(sdl3_joysticks[index]);
				}
			}
		} else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
			auto index = blah_sdl_find_joystick_index(sdl3_joysticks,
													  event.jdevice.which);
			if (index >= 0) {
				if (SDL_IsGamepad(index) == false)
					state.controllers[index].on_press(
						(Button) event.jbutton.button);
			}
		} else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
			auto index = blah_sdl_find_joystick_index(sdl3_joysticks,
													  event.jdevice.which);
			if (index >= 0) {
				if (SDL_IsGamepad(index) == false)
					state.controllers[index].on_release(
						(Button) event.jbutton.button);
			}
		} else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
			auto index = blah_sdl_find_joystick_index(sdl3_joysticks,
													  event.jdevice.which);
			if (index >= 0) {
				if (SDL_IsGamepad(index) == false) {
					float value;
					if (event.jaxis.value >= 0)
						value = event.jaxis.value / 32767.0f;
					else
						value = event.jaxis.value / 32768.0f;
					state.controllers[index].on_axis((Axis) event.jaxis.axis,
													 value);
				}
			}
		}
		// Gamepad Controller
		else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
			auto index = event.cdevice.which;
			if (index >= 0 && index < Input::max_controllers) {
				auto ptr = sdl3_gamepads[index] = SDL_OpenGamepad(index);
				auto name						= SDL_GetGamepadName(ptr);
				auto vendor						= SDL_GetGamepadVendor(ptr);
				auto product					= SDL_GetGamepadProduct(ptr);
				auto version = SDL_GetGamepadProductVersion(ptr);

				state.controllers[index].on_connect(name, 1, 15, 6, vendor,
													product, version);
			}
		} else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
			auto index =
				blah_sdl_find_gamepad_index(sdl3_gamepads, event.cdevice.which);
			if (index >= 0) {
				state.controllers[index].on_disconnect();
				SDL_CloseGamepad(sdl3_gamepads[index]);
			}
		} else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
			auto index =
				blah_sdl_find_gamepad_index(sdl3_gamepads, event.gbutton.which);

			if (index >= 0) {
				Button button = Button::None;

				if (event.gbutton.button < 15)
					button = (Button) event.gbutton.button;

				// NOTE: These map directly to Engine Buttons enum!
				state.controllers[index].on_press(button);
			}
		} else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
			auto index =
				blah_sdl_find_gamepad_index(sdl3_gamepads, event.gbutton.which);

			if (index >= 0) {
				Button button = Button::None;

				if (event.gbutton.button < 15)
					button = (Button) event.gbutton.button;

				// NOTE: These map directly to Engine Buttons enum!
				state.controllers[index].on_release(button);
			}
		} else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
			auto index =
				blah_sdl_find_gamepad_index(sdl3_gamepads, event.gaxis.which);

			if (index >= 0) {
				Axis axis = Axis::None;

				if (event.gaxis.axis < 6)
					axis = (Axis) event.gaxis.axis;

				float value;

				if (event.gaxis.value >= 0)
					value = event.gaxis.value / 32767.0f;
				else
					value = event.gaxis.value / 32768.0f;

				state.controllers[index].on_axis(axis, value);
			}
		}
	}
}

void Platform::sleep(int milliseconds) {
	if (milliseconds >= 0)
		SDL_Delay((u32) milliseconds);
}

void Platform::present() {
	if (App::renderer().type == RendererType::OpenGL)
		SDL_GL_SwapWindow(sdl3_window);

	// display the window
	// this avoids a short black screen on macOS
	if (!sdl3_displayed) {
		SDL_ShowWindow(sdl3_window);
		sdl3_displayed = true;
	}
}

void Platform::set_app_flags(u32 flags) {
	// Toggle Fullscreen
	SDL_SetWindowFullscreen(sdl3_window, (flags & Flags::Fullscreen) != 0);

	// Toggle Resizable
	SDL_SetWindowResizable(sdl3_window,
						   ((flags & Flags::Resizable) != 0) ? true : false);

	// Toggle V-Sync for OpenGL
#ifndef __EMSCRIPTEN__
	if (App::renderer().type == RendererType::OpenGL)
		SDL_GL_SetSwapInterval(((flags & Flags::VSync) != 0) ? 1 : 0);
#endif
}

const char *Platform::get_title() {
	return SDL_GetWindowTitle(sdl3_window);
}

void Platform::set_title(const char *title) {
	SDL_SetWindowTitle(sdl3_window, title);
}

void Platform::get_position(int *x, int *y) {
	SDL_GetWindowPosition(sdl3_window, x, y);
}

void Platform::set_position(int x, int y) {
	SDL_SetWindowPosition(sdl3_window, x, y);
}

bool Platform::get_focused() {
	auto flags = SDL_GetWindowFlags(sdl3_window);
	return (flags & SDL_WINDOW_INPUT_FOCUS) != 0 &&
		   (flags & SDL_WINDOW_MINIMIZED) == 0;
}

void Platform::get_size(int *width, int *height) {
	SDL_GetWindowSize(sdl3_window, width, height);
}

void Platform::set_size(int width, int height) {
	SDL_SetWindowSize(sdl3_window, width, height);
}

void Platform::get_draw_size(int *width, int *height) {
	SDL_GetWindowSizeInPixels(sdl3_window, width, height);
}

const char *Platform::app_path() {
	if (sdl3_base_path_value == nullptr)
		sdl3_base_path_value = (char *) SDL_GetBasePath();
	return sdl3_base_path_value;
}

const char *Platform::user_path() {
	if (sdl3_user_path_value == nullptr) {
		auto &config		 = App::config();
		sdl3_user_path_value = SDL_GetPrefPath(nullptr, config.name);
	}

	return sdl3_user_path_value;
}

FileRef Platform::file_open(const char *path, FileMode mode) {
	const char *sdl_mode = "";

	switch (mode) {
		case FileMode::OpenRead:
			sdl_mode = "rb";
			break;
		case FileMode::Open:
			sdl_mode = "r+b";
			break;
		case FileMode::CreateWrite:
			sdl_mode = "wb";
			break;
		case FileMode::Create:
			sdl_mode = "w+b";
			break;
	}

	auto ptr = SDL_IOFromFile(path, sdl_mode);
	if (!ptr)
		return FileRef();

	return FileRef(new SDL3_File(ptr));
}

bool Platform::file_exists(const char *path) {
	return std::filesystem::is_regular_file(path);
}

bool Platform::file_delete(const char *path) {
	return std::filesystem::remove(path);
}

bool Platform::dir_create(const char *path) {
	return std::filesystem::create_directories(path);
}

bool Platform::dir_exists(const char *path) {
	return std::filesystem::is_directory(path);
}

bool Platform::dir_delete(const char *path) {
	return std::filesystem::remove_all(path) > 0;
}

void Platform::dir_enumerate(Vector<FilePath> &list, const char *path,
							 bool recursive) {
	if (std::filesystem::is_directory(path)) {
		if (recursive) {
			for (auto &p: std::filesystem::recursive_directory_iterator(path))
				list.emplace_back(p.path().string().c_str());
		} else {
			for (auto &p: std::filesystem::directory_iterator(path))
				list.emplace_back(p.path().string().c_str());
		}
	}
}

void Platform::dir_explore(const char *path) {
#if _WIN32

	ShellExecute(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);

#elif __linux__

	system(String::fmt("xdg-open \"%s\"", path).cstr());

#else

	system(String::fmt("open \"%s\"", path).cstr());

#endif
}

void Platform::set_clipboard(const char *text) {
	SDL_SetClipboardText(text);
}

const char *Platform::get_clipboard() {
	// free previous clipboard text
	if (sdl3_clipboard_text != nullptr)
		SDL_free(sdl3_clipboard_text);

	sdl3_clipboard_text = SDL_GetClipboardText();
	return sdl3_clipboard_text;
}

void *Platform::gl_get_func(const char *name) {
	return (void *) SDL_GL_GetProcAddress(name);
}

void *Platform::gl_context_create() {
	void *pointer = SDL_GL_CreateContext(sdl3_window);
	if (pointer == nullptr)
		Log::error("SDL_GL_CreateContext failed: %s", SDL_GetError());
	return pointer;
}

void Platform::gl_context_make_current(void *context) {
	SDL_GL_MakeCurrent(sdl3_window, (SDL_GLContext) context);
}

void Platform::gl_context_destroy(void *context) {
	SDL_GL_DestroyContext((SDL_GLContext) context);
}

void *Platform::d3d11_get_hwnd() {
#if _WIN32
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(sdl3_window, &info);
	return info.info.win.window;
#else
	return nullptr;
#endif
}

void Platform::open_url(const char *url) {
	SDL_OpenURL(url);
}
