#pragma once
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include "RenderDeviceOpenGL.h"
namespace ParaEngine
{
	class CParaEngineAppAndroid;
	class IRenderDevice;
	class IRenderContext;
	class IRenderWindow;
    class AppDelegate 
    {
    public:
        AppDelegate(struct android_app* app);
        ~AppDelegate();
        void Run();
    private:
        struct android_app* m_State;
        static void app_handle_command(struct android_app* app, int32_t cmd);
        static int32_t app_handle_input(struct android_app* app, AInputEvent* event);
    protected:
        // App commands
        virtual void OnStart();
        virtual void OnStop();
        virtual void OnPause();
        virtual void OnResume();
        virtual void OnDestroy();
        virtual void OnInitWindow();
        virtual void OnTermWindow();
        virtual void OnWindowResized();
	protected:
		CParaEngineAppAndroid* m_ParaEngineApp;
		IRenderDevice* m_RenderDevice;
		IRenderContext* m_RenderContext;
		IRenderWindow* m_RenderWindow;
    };
}