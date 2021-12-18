#include "vice.h"
#include "interrupt.h"
#include "cmdline.h"
#include "video.h"
#include "videoarch.h"
#include "palette.h"
#include "viewport.h"
#include "keyboard.h"
#include "lib.h"
#include "log.h"
#include "ui.h"
#include "vsync.h"
#include "raster.h"
#include "sound.h"
#include "machine.h"
#include "resources.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro-core.h"

int machine_ui_done = 0;

static const cmdline_option_t cmdline_options[] = {
     { NULL }
};

int video_init_cmdline_options(void)
{
   return cmdline_register_options(cmdline_options);
}

int video_arch_cmdline_options_init(void)
{
   return cmdline_register_options(cmdline_options);
}

void video_canvas_resize(struct video_canvas_s *canvas, char resize_canvas)
{
}

video_canvas_t *video_canvas_create(video_canvas_t *canvas, 
      unsigned int *width, unsigned int *height, int mapped)
{
   canvas->videoconfig->rendermode = VIDEO_RENDER_RGB_1X1;
   canvas->depth = 8*pix_bytes;
   video_canvas_set_palette(canvas, canvas->palette);
   return canvas;
}

void video_canvas_destroy(struct video_canvas_s *canvas)
{
}

void video_arch_canvas_init(struct video_canvas_s *canvas)
{
}

char video_canvas_can_resize(video_canvas_t *canvas)
{
   return 1;
}

int video_canvas_set_palette(struct video_canvas_s *canvas,
                             struct palette_s *palette)
{
   unsigned int i, col = 0;

   if (!canvas || !palette) {
      return 0; /* No palette, nothing to do */
   }

   canvas->palette = palette;

   for (i = 0; i < palette->num_entries; i++) {
      if (pix_bytes == 2)
         col = RGB565(palette->entries[i].red, palette->entries[i].green, palette->entries[i].blue);
      else
         col = palette->entries[i].red << 16 | palette->entries[i].green << 8 | palette->entries[i].blue;

      video_render_setphysicalcolor(canvas->videoconfig, i, col, canvas->depth);
   }

   for (i = 0; i < 256; i++) {
      if (pix_bytes == 2)
         video_render_setrawrgb(i, RGB565(i, 0, 0), RGB565(0, i, 0), RGB565(0, 0, i));
      else
         video_render_setrawrgb(i, i << 16, i << 8, i);
   }
   video_render_initraw(canvas->videoconfig);

   return 0;
}

void video_canvas_refresh(struct video_canvas_s *canvas,
      unsigned int xs, unsigned int ys,
      unsigned int xi, unsigned int yi,
      unsigned int w, unsigned int h)
{ 
   unsigned i = 0;
   unsigned j = 0;
   unsigned color_diff = 0;
   unsigned zoom_bottom_border = 0;

#ifdef RETRO_DEBUG
   printf("XS:%d YS:%d XI:%d YI:%d W:%d H:%d\n",xs,ys,xi,yi,w,h);
#endif

   video_canvas_render(
         canvas, (uint8_t *)&retro_bmp,
         retrow, retroh,
         retroXS, retroYS,
         0, 0, /*xi, yi,*/
         retrow*pix_bytes, 8*pix_bytes
   );

   if (!retroh || zoom_mode_id < ZOOM_MODE_AUTO)
      return;

   /* Reset to maximum zoom */
   vice_raster.first_line = ZOOM_TOP_BORDER;
   vice_raster.last_line  = vice_raster.first_line + ZOOM_HEIGHT_MAX;

   switch (zoom_mode_id)
   {
      case ZOOM_MODE_AUTO:
         /* Pixel color per row must return to the background color
          * in order to count as a show-worthy row, otherwise
          * loaders with flashing borders would count as hits */
         color_diff         = 3000 * pix_bytes;
         zoom_bottom_border = ZOOM_TOP_BORDER + ZOOM_HEIGHT_MAX;

         /* Top border, start from top */
         for (i = 0; i < ZOOM_TOP_BORDER && !vice_raster.blanked; i++)
         {
            unsigned row   = i * (retrow << (pix_bytes >> 2));
            unsigned col_x = row + (ZOOM_LEFT_BORDER + 3) * (pix_bytes >> 1);
            unsigned color = retro_bmp[col_x];
            unsigned found = 0;

            for (j = ZOOM_LEFT_BORDER; j < retrow - ZOOM_LEFT_BORDER; j++)
            {
               unsigned pixel = row + j * (pix_bytes >> 1);

               if (abs(retro_bmp[pixel] - color) > color_diff)
                  found++;

               /* Crazy Cars III FFS.. */
               if (i < 6 && j > 265 && j < 290)
                  found = 0;

               if (found && retro_bmp[pixel] == color)
               {
#if 0
                  printf("%s: FRST %3d %3d, %2d %d\n", __func__, i, j, found, color);
#endif
                  vice_raster.first_line = i;
                  break;
               }
            }

            if (vice_raster.first_line < ZOOM_TOP_BORDER)
               break;
         }

         /* Allow bottom border upwards a few rows if top border is not used much.
          * For oddly shifted cases: Alien Syndrome, Out Run Europa */
         if (vice_raster.first_line > 20)
            zoom_bottom_border -= 5;

         /* Bottom border, start from bottom, almost */
         for (i = retroh - 2; i > zoom_bottom_border && !vice_raster.blanked; i--)
         {
            unsigned row   = i * (retrow << (pix_bytes >> 2));
            unsigned col_x = row + (ZOOM_LEFT_BORDER + 3) * (pix_bytes >> 1);
            unsigned color = retro_bmp[col_x];
            unsigned found = 0;

            for (j = ZOOM_LEFT_BORDER; j < retrow - ZOOM_LEFT_BORDER; j++)
            {
               unsigned pixel = row + j * (pix_bytes >> 1);

               if (abs(retro_bmp[pixel] - color) > color_diff)
                  found++;

               if (found && retro_bmp[pixel] == color)
               {
#if 0
                  printf("%s: LAST %3d %3d, %2d %d\n", __func__, i, j, found, color);
#endif
                  vice_raster.last_line = i + 1;
                  break;
               }
            }

            if (vice_raster.last_line > ZOOM_TOP_BORDER + ZOOM_HEIGHT_MAX)
               break;
         }

         /* Result pondering with stabilization period */
         if (vice_raster.first_line != vice_raster.first_line_prev ||
             vice_raster.last_line  != vice_raster.last_line_prev)
         {
            vice_raster.counter = 0;

            /* Require a line step bigger than one */
            if (abs(vice_raster.first_line_prev - vice_raster.first_line) > 1)
               vice_raster.first_line_maybe = vice_raster.first_line;
            if (abs(vice_raster.last_line_prev - vice_raster.last_line) > 1)
               vice_raster.last_line_maybe  = vice_raster.last_line;
         }
         else
         if (vice_raster.first_line  == vice_raster.first_line_maybe &&
             vice_raster.last_line   == vice_raster.last_line_maybe &&
             (vice_raster.first_line != vice_raster.first_line_active ||
              vice_raster.last_line  != vice_raster.last_line_active))
         {
            vice_raster.counter++;

            if (vice_raster.counter > 3)
            {
               zoom_mode_id_prev   = -1;
               vice_raster.counter = 0;
               vice_raster.first_line_active = vice_raster.first_line;
               vice_raster.last_line_active  = vice_raster.last_line;
            }
         }
         break;

      case ZOOM_MODE_AUTO_DISABLE:
         if (!vice_raster.blanked)
         {
            vice_raster.first_line = 0;
            vice_raster.last_line  = retroh;
         }

         /* Result pondering with stabilization period */
         if (vice_raster.first_line != vice_raster.first_line_prev ||
             vice_raster.last_line  != vice_raster.last_line_prev)
         {
            vice_raster.counter = 0;
            vice_raster.first_line_maybe = vice_raster.first_line;
            vice_raster.last_line_maybe  = vice_raster.last_line;
         }
         else
         if (vice_raster.first_line  == vice_raster.first_line_maybe &&
             vice_raster.last_line   == vice_raster.last_line_maybe &&
             (vice_raster.first_line != vice_raster.first_line_active ||
              vice_raster.last_line  != vice_raster.last_line_active))
         {
            vice_raster.counter++;

            if (vice_raster.counter > 1)
            {
               zoom_mode_id_prev   = -1;
               vice_raster.counter = 0;
               vice_raster.first_line_active = vice_raster.first_line;
               vice_raster.last_line_active  = vice_raster.last_line;
            }
         }
         break;

      default:
         break;
   }

   vice_raster.first_line_prev = vice_raster.first_line;
   vice_raster.last_line_prev  = vice_raster.last_line;
   vice_raster.blanked         = 0;
}

int video_init()
{
   return 0;
}

void video_shutdown()
{
}

int video_arch_resources_init()
{
   return 0;
}

void video_arch_resources_shutdown()
{
}

void fullscreen_capability(struct cap_fullscreen_s *cap_fullscreen)
{
}
