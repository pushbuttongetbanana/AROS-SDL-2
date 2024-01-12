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

#include "../SDL_sysvideo.h"
#include "../../core/morphos/SDL_misc.h"

#include <devices/clipboard.h>
#include <libraries/charsets.h>
#include <proto/exec.h>
#include <proto/iffparse.h>

#define ID_FTXT	MAKE_ID('F','T','X','T')
#define ID_CHRS	MAKE_ID('C','H','R','S')
#define ID_UTF8	MAKE_ID('U','T','F','8')

STATIC APTR clipboard_open(void)
{
	struct IOClipReq *io;
	struct MsgPort *mp;

	mp = CreateMsgPort();

	if ((io = (struct IOClipReq *)CreateIORequest(mp,sizeof(struct IOClipReq)))) {
		if (!(OpenDevice("clipboard.device", 0, (struct IORequest *)io, 0))) {
			return io;
		}

		DeleteIORequest((struct IORequest *)io);
	}

	DeleteMsgPort(mp);
	return NULL;
}

STATIC ULONG clipboard_write_data(struct IOClipReq *io, CONST_APTR data, ULONG len)
{
	LONG rc;

	io->io_Command = CMD_WRITE;
	io->io_Data    = (APTR)data;
	io->io_Length  = len;
	DoIO( (struct IORequest *)io);

	if (io->io_Actual != len) {
		io->io_Error = 1;
	}

	rc = io->io_Error ? FALSE : TRUE;
	return rc;
}


STATIC VOID clipboard_pad_text(struct IOClipReq *io, ULONG textlen)
{
	if (textlen & 1)
		clipboard_write_data(io, "", 1);
}


STATIC ULONG clipboard_write_header_and_text(struct IOClipReq *io, CONST_STRPTR string, ULONG slen, ULONG ulen)
{
	ULONG rc;

	struct
	{
		ULONG form;
		ULONG totalsize;
		ULONG ftxt;
		ULONG type;
		ULONG strlen;
	} iffheader;

	io->io_Offset = 0;
	io->io_Error  = 0;
//	io->io_ClipID = 0;

	iffheader.form      = ID_FORM;
	iffheader.totalsize = (slen & 1 ? slen + 1 : slen) + (ulen & 1 ? ulen + 1 : ulen) + 12 + 8;
	iffheader.ftxt      = ID_FTXT;
	iffheader.type      = ID_CHRS;
	iffheader.strlen    = slen;

	rc = FALSE;

	if (clipboard_write_data(io, &iffheader, sizeof(iffheader))) {
		if (clipboard_write_data(io, string, slen)) {
			clipboard_pad_text(io, slen);
			rc = TRUE;
		}
	}

	return rc;
}


STATIC ULONG clipboard_write_utf8(struct IOClipReq *io, CONST_STRPTR utext, ULONG ulen)
{
	ULONG rc;

	struct
	{
		ULONG type;
		ULONG strlen;
	} utf8_header;

	/* FIXME: For correct operation we should also store font name. Used font
	 * is relevant with guides written in Japanese for example.
	 */

	utf8_header.type   = ID_UTF8;
	utf8_header.strlen = ulen;

	rc = FALSE;

	if (clipboard_write_data(io, &utf8_header, sizeof(utf8_header))) {
		if (clipboard_write_data(io, utext, ulen)) {
			clipboard_pad_text(io, ulen);
			rc = TRUE;
		}
	}

	return rc;
}

STATIC VOID clipboard_finalize(struct IOClipReq *io)
{
	io->io_Command = CMD_UPDATE;
	DoIO((struct IORequest *)io);
}

STATIC void clipboard_close(struct IOClipReq *io)
{
	if (io) {
		struct MsgPort *mp = io->io_Message.mn_ReplyPort;

		CloseDevice((struct IORequest *)io);
		DeleteIORequest((struct IORequest *)io);
		DeleteMsgPort(mp);
	}
}

int
MOS_SetClipboardText(_THIS, const char *text)
{
	APTR ctx;
	int rc = -1;

	if ((ctx = clipboard_open())) {
		char *stext = MOS_ConvertText(text, MIBENUM_UTF_8, MIBENUM_SYSTEM);

		if (stext) {
			int ulen = strlen(text);

			if (clipboard_write_header_and_text(ctx, stext, strlen(stext), ulen)) {
				if (clipboard_write_utf8(ctx, text, ulen))
					rc = 0;
			}

			SDL_free(stext);
			clipboard_finalize(ctx);
		}
		
		clipboard_close(ctx);
	}

	return rc;
}

char *
MOS_GetClipboardText(_THIS)
{
	struct IFFHandle *clip = AllocIFF();
	char *text = NULL;

	if (clip) {
		clip->iff_Stream = (IPTR)OpenClipboard(0);

		if (clip->iff_Stream) {
			InitIFFasClip(clip);

			if (!OpenIFF(clip, IFFF_READ)) {
				if (!StopChunk(clip, ID_FTXT, ID_CHRS) && !StopChunk(clip, ID_FTXT, ID_UTF8)) {
					BOOL done = FALSE;

					while (done == FALSE && !ParseIFF(clip, IFFPARSE_SCAN)) {
						struct ContextNode *cn = CurrentChunk(clip);

						if (cn) {
							LONG size = cn->cn_Size;

							if (cn->cn_Type == ID_FTXT && size > 0) {
								if (cn->cn_ID == ID_CHRS && !text) {
									char *tmp = SDL_malloc(size + 1);

									if (tmp) {
										ReadChunkBytes(clip, tmp, size);
										tmp[size] = '\0';
										text = MOS_ConvertText(tmp, MIBENUM_SYSTEM, MIBENUM_UTF_8);
										SDL_free(tmp);
									}
								}
								else if (cn->cn_ID == ID_UTF8) {
									if (text)
										SDL_free(text);

									if ((text = SDL_malloc(size + 1))) {
										ReadChunkBytes(clip, text, size);
										text[size] = '\0';
										done = TRUE;
										break;
									}
								}
							}
						}
					}
				}

				CloseIFF(clip);
			}

			CloseClipboard((struct ClipboardHandle *)clip->iff_Stream);
		}

		FreeIFF(clip);
	}

	if (text == NULL) {
		if ((text = SDL_malloc(1)))
			text[0] = '\0';
	}

	return text;
}

SDL_bool
MOS_HasClipboardText(_THIS)
{
	SDL_bool result = SDL_FALSE;
	char *text = MOS_GetClipboardText(_this);

	if (text) {
		result = text[0] != '\0' ? SDL_TRUE : SDL_FALSE;
		SDL_free(text);
	}

	return result;
}

/* vi: set ts=4 sw=4 expandtab: */
