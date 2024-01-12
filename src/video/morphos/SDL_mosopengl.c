/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MORPHOS

#include "SDL_error.h"
#include "SDL_syswm.h"
#include "../SDL_sysvideo.h"
#include "SDL_mosvideo.h"
#include "SDL_mosmodes.h"
#include "SDL_moswindow.h"
#include "../../core/morphos/SDL_library.h"

#include <proto/exec.h>
#include <proto/tinygl.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <tgl/gl.h>
#include <tgl/gla.h>
#include <tgl/glu.h>

GLContext *__tglContext;

extern struct SDL_Library *SDL2Base;

int
MOS_GL_LoadLibrary(_THIS, const char *path)
{

	if (!TinyGLBase)
		TinyGLBase = OpenLibrary("tinygl.library", 53); 

	if (TinyGLBase) {
			UWORD TinyGl_Version = TinyGLBase->lib_Version;
			UWORD TinyGl_Revision = TinyGLBase->lib_Revision;
			D("[%s] tinygl.library version %d.%d\n", __FUNCTION__, TinyGl_Version, TinyGl_Revision);
			if (TinyGl_Version < 53 || (TinyGl_Version == 53 && TinyGl_Revision < 7))
			{
				// tingl < 53.7
				SDL_SetError("Failed to open tinygl.library 53.7+");
				return -1;
			}
			if (SDL2Base->MyTinyGLBase)				
				*SDL2Base->MyTinyGLBase = TinyGLBase;	
			
			return 0;
	} else 
		SDL_SetError("Failed to open tinygl.library 53.7+");

	return -1;
}

void *
MOS_GL_GetProcAddress(_THIS, const char *proc)
{
	void *func = NULL;
	func = tglGetProcAddress(proc);
	if (!func) {
    	SDL_SetError("Couldn't find OpenGL symbol");
		return NULL;
    }
	return func;
}

void
MOS_GL_UnloadLibrary(_THIS)
{
	D("[%s]\n", __FUNCTION__);

	if (SDL2Base->MyTinyGLBase && *SDL2Base->MyTinyGLBase && TinyGLBase) {
		CloseLibrary(TinyGLBase);
		*SDL2Base->MyTinyGLBase = TinyGLBase = NULL;
	}
}

SDL_bool MOS_GL_AllocBitmap(_THIS, SDL_Window * window)
{
	D("[%s]\n", __FUNCTION__);
	SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
	SDL_VideoData *vd = data->videodata;

	if (data->bitmap != NULL) {
		//D("[%s] FreeBitMap\n", __FUNCTION__);
		FreeBitMap(data->bitmap);
		data->bitmap = NULL;
	}
	
	int h = 0, w = 0;
	struct BitMap * friend_bitmap;
	ULONG depth = 0;
	if (data->winflags & SDL_MOS_WINDOW_FULLSCREEN_DESKTOP) {
		//D("[%s] DESKTOP FULLSCREEN\n", __FUNCTION__);
		w = vd->WScreen->Width;
		h = vd->WScreen->Height;
		friend_bitmap = data->win->RPort->BitMap;	
	} else if (data->winflags & SDL_MOS_WINDOW_FULLSCREEN){
		//D("[%s] REAL FULLSCREEN\n", __FUNCTION__);
		w = vd->CustomScreen->Width;
		h = vd->CustomScreen->Height;
		friend_bitmap=vd->CustomScreen->RastPort.BitMap;
	} else {
		//D("[%s] WINDOWED\n", __FUNCTION__);
		w = window->w;
		h= window->h;	
		friend_bitmap =	data->win->RPort->BitMap;	
	}   
	depth = GetBitMapAttr(friend_bitmap, BMA_DEPTH);
	//D("[%s] AllocBitMap w=%d h=%d depth=%d\n", __FUNCTION__, w, h, depth);
	return (data->bitmap = AllocBitMap(w, h, depth, BMF_MINPLANES|BMF_DISPLAYABLE|BMF_3DTARGET, friend_bitmap)) != NULL;

}

SDL_bool MOS_GL_InitContext(_THIS, SDL_Window * window)
{
	D("[%s]\n", __FUNCTION__);
	SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
	SDL_VideoData *vd = data->videodata;
	
	if (data->__tglContext != NULL) {
		//D("[%s] GLADestroyContext\n", __FUNCTION__);
		GLADestroyContext(data->__tglContext);
		data->__tglContext = NULL;
		if (data->bitmap != NULL) {
			//D("[%s] FreeBitMap\n", __FUNCTION__);
			FreeBitMap(data->bitmap);
			data->bitmap = NULL;
		}
	}

	struct TagItem tgltags[] =
	{
		{TAG_IGNORE, 0},
		{TGL_CONTEXT_STENCIL, TRUE},
		{TAG_DONE}
	};	
	

	if (MOS_GL_AllocBitmap(_this, window)) {	
		//D("[%s] TGL_CONTEXT_BITMAP \n", __FUNCTION__);			
		tgltags[0].ti_Tag = TGL_CONTEXT_BITMAP;
		tgltags[0].ti_Data = (IPTR)data->bitmap;
	} else {
		if (vd->CustomScreen != NULL && (data->winflags & SDL_MOS_WINDOW_FULLSCREEN) && (!(data->winflags & SDL_MOS_WINDOW_FULLSCREEN_DESKTOP))) {
			tgltags[0].ti_Tag = TGL_CONTEXT_SCREEN;
			tgltags[0].ti_Data = (IPTR)vd->CustomScreen;		
		} else {
			tgltags[0].ti_Tag = TGL_CONTEXT_WINDOW;
			tgltags[0].ti_Data = (IPTR)data->win;			
		}
	}
		
	// Initialize new context
	int success = GLAInitializeContext(__tglContext, tgltags);
	if (success) {
		data->__tglContext = __tglContext;
		
		SDL_bool withnewextension = SDL_TRUE;
		
		char *val = val = MOS_getenv("SDL_RENDER_OPENGL_SHADERS");
		if (val && strlen(val)>0 && strcmp(val, "0")==0) 
			withnewextension = SDL_FALSE;
		
		if (withnewextension)
			tglEnableNewExtensions(0); // Active TinyGL New extensions
	}
	//D("[%s] success=%d\n", __FUNCTION__, success);

	return success ? SDL_TRUE : SDL_FALSE;		
}

SDL_GLContext
MOS_GL_CreateContext(_THIS, SDL_Window * window)
{
	SDL_WindowData *data = window->driverdata;
	
	GLContext *glcont = GLInit();
	if (glcont) {
		__tglContext = glcont;
		int success = MOS_GL_InitContext(_this, window);
		if (success) {
			D("[%s] MOS_GL_InitContext SUCCES 0x%08lx, data->__tglContext=0x%08lx\n", __FUNCTION__, glcont, data->__tglContext);
			*SDL2Base->MyGLContext = glcont;
			return glcont;
		} else {
			D("[%s] MOS_GL_InitContext FAILED 0x%08lx, data->__tglContext=0x%08lx\n", __FUNCTION__, glcont, data->__tglContext);
			if (data->bitmap != NULL) {
				D("[%s] FreeBitMap\n", __FUNCTION__);
				FreeBitMap(data->bitmap);
				data->bitmap = NULL;
			}
			GLClose(glcont);
			*SDL2Base->MyGLContext = data->__tglContext = __tglContext = NULL;
			
			SDL_SetError("Couldn't initialize TinyGL context");
		}
	} else {
		SDL_SetError("Couldn't create TinyGL context");
	}

	return NULL;
}

int
MOS_GL_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context)
{
	D("[%s] context 0x%08lx\n", __FUNCTION__, context);
	if (context)
		*SDL2Base->MyGLContext = __tglContext = context;
	
	return 0;
}

void
MOS_GL_GetDrawableSize(_THIS, SDL_Window * window, int *w, int *h)
{
	SDL_WindowData * data = window->driverdata;
	if (window) {

		if (w)
			*w = data->win->Width - data->win->BorderLeft - data->win->BorderRight;
		if (h)
			*h = data->win->Height - data->win->BorderTop - data->win->BorderBottom;
		
	}
		
}

int
MOS_GL_SetSwapInterval(_THIS, int interval)
{
	SDL_VideoData *data = _this->driverdata;
	
	switch (interval) {
		case 0:
		case 1:
			// always VSYNC in fullscreen
			data->vsyncEnabled = /*data->CustomScreen != NULL ? TRUE : */(interval ? TRUE : FALSE);
			return 0;
		default:
			return -1;
	}	
}

int
MOS_GL_GetSwapInterval(_THIS)
{
	SDL_VideoData *data = _this->driverdata;
	return data->vsyncEnabled ? 1 : 0;
}

int
MOS_GL_SwapWindow(_THIS, SDL_Window * window)
{
	SDL_WindowData *data = (SDL_WindowData *) window->driverdata;

	if (!data->win || !data->__tglContext || !__tglContext)
		return -1;

	SDL_VideoData *video = _this->driverdata;
	if (video->vsyncEnabled /*&& data->CustomScreen == NULL*/) {
		BOOL displayed = getv(data->win->WScreen, SA_Displayed);
		if (displayed) {
			WaitBOVP(&data->win->WScreen->ViewPort);
		}
	}
	
	GLASwapBuffers(data->__tglContext);
	
	if (data->bitmap != NULL) {
		
		BltBitMapRastPort(data->bitmap, 0, 0, data->win->RPort, data->win->BorderLeft, data->win->BorderTop, 
				window->w, window->h, 0xc0);
	}
	
	
	return 0;
}

void
MOS_GL_DeleteContext(_THIS, SDL_GLContext context)
{
	D("[%s] context 0x%08lx\n", __FUNCTION__, context);

	if (TinyGLBase != NULL && context) {
		SDL_Window *sdlwin;

		for (sdlwin = _this->windows; sdlwin; sdlwin = sdlwin->next) {

			SDL_WindowData *data = sdlwin->driverdata;

			if (data->__tglContext == context) {
                D("[%s] Found TinyGL context 0x%08lx, clearing window binding\n", __FUNCTION__, context);
                GLADestroyContext(context);
				data->__tglContext = NULL;
				if (data->bitmap != NULL) {
					D("[%s] FreeBitMap\n", __FUNCTION__);
					FreeBitMap(data->bitmap);
					data->bitmap = NULL;
				}
			
			}
		}
		GLClose(context);
		*SDL2Base->MyGLContext = __tglContext = NULL;
	}
}

int
MOS_GL_ResizeContext(_THIS, SDL_Window *window)
{
	
	SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
	D("[%s] Context=0x%08lx data->__tglContext=0x%08lx\n", __FUNCTION__, __tglContext, data->__tglContext);
	if (data->__tglContext == NULL || __tglContext == NULL || data->win == NULL) {
		return -1;
	}
	
	return (MOS_GL_InitContext(_this, window) ? 0 : -1);
}

#endif /* SDL_VIDEO_DRIVER_MORPHOS */
