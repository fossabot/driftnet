/*
 * display.c:
 * Display images gathered by driftnet.
 *
 * Copyright (c) 2001 Chris Lightfoot. All rights reserved.
 * Email: chris@ex-parrot.com; WWW: http://www.ex-parrot.com/~chris/
 *
 */

#ifndef NO_DISPLAY_WINDOW

static const char rcsid[] = "$Id: display.c,v 1.19 2004/04/26 14:42:36 chris Exp $";

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif
#include "compat.h"

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h> /* On many systems (Darwin...), stdio.h is a prerequisite. */
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <sys/stat.h>

#include "log.h"
#include "options.h"
#include "tmpdir.h"
#include "driftnet.h"
#include "img.h"

/* The border, in pixels, around images displayed in the window. */
#define BORDER  6

extern int beep; /* in driftnet.c */

static GtkWidget *window, *darea;
static GdkWindow *drawable;

static int width, height, wrx, wry, rowheight;
static img backing_image;

struct imgrect {
    char *filename;
    int x, y, w, h;
};

static int nimgrects;
static struct imgrect *imgrects;

gint delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    log_msg(LOG_INFO, "display child shutting down\n");
    return FALSE;   /* do destroy window */
}

/* make_backing_image:
 * Create the img structure which represents our back-buffer. */
void make_backing_image() {
    img I;
    I = img_new_blank(width, height);
    img_alloc(I);
/*    wry += height - backing_image->height;
    if (wry < BORDER || wry > height - BORDER)
        wry = height - BORDER;*/
    if (backing_image) {
        int w2, h2;
        struct imgrect *ir;

        /* Copy old contents of backing image to ll corner of new one. */
        w2 = backing_image->width;
        if (w2 > width) w2 = width;
        h2 = backing_image->height;
        if (h2 > height) h2 = height;

        img_simple_blt(I, 0, height - h2, backing_image, 0, backing_image->height - h2, w2, h2);

        /* Move all of the image rectangles. */
        for (ir = imgrects; ir < imgrects + nimgrects; ++ir) {
            if (ir->filename) {
                ir->y += height - backing_image->height;

                /* Possible it has scrolled off the window. */
                if (ir->x > width || ir->y + ir->h < 0) {
                    unlink(ir->filename);
                    xfree(ir->filename);
                    memset(ir, 0, sizeof *ir);
                }
            }
        }

        /* Adjust placement of new images. */
        if (wrx >= w2) wrx = w2;
        
        img_delete(backing_image);
    }
    backing_image = I;
    wrx = BORDER;
    wry = height - BORDER;
    rowheight = 2 * BORDER;
}

/* update_window:
 * Copy the backing image onto the window. */
void update_window() {
    if (backing_image) {
        GdkGC *gc;
        gc = gdk_gc_new(drawable);
        gdk_draw_rgb_32_image(drawable, gc, 0, 0, width, height, GDK_RGB_DITHER_NORMAL, (guchar*)backing_image->flat, sizeof(pel) * width);
        g_object_unref(gc);
    }
}

/* scroll_backing_image:
 * Scroll the image up a bit, to make room for a new image. */
void scroll_backing_image(const int dy) {
    pel **row1, **row2;
    struct imgrect *ir;
    
    for (row1 = backing_image->data, row2 = backing_image->data + dy;
         row2 < backing_image->data + height; ++row1, ++row2)
        memcpy(*row1, *row2, width * sizeof(pel));

    for (row2 = row1; row2 < backing_image->data + height; ++row2)
        memset(*row2, 0, width * sizeof(pel));

    for (ir = imgrects; ir < imgrects + nimgrects; ++ir) {
        if (ir->filename) {
            ir->y -= dy;

            /* scrolled off bottom, no longer in use. */
            if ((ir->y + ir->h) < 0) {
                unlink(ir->filename);
                xfree(ir->filename);
                memset(ir, 0, sizeof *ir);
            }
        }
    }
}

/* add_image_rectangle:
 * Add a rectangle representing the location of an image to the list, so that
 * we can do hit-tests against it. */
void add_image_rectangle(const char *filename, const int x, const int y, const int w, const int h) {
    struct imgrect *ir;
    for (ir = imgrects; ir < imgrects + nimgrects; ++ir) {
        if (!ir->filename)
            break;
    }
    if (ir == imgrects + nimgrects) {
        imgrects = xrealloc(imgrects, 2 * nimgrects * sizeof *imgrects);
        memset(imgrects + nimgrects, 0, nimgrects * sizeof *imgrects);
        ir = imgrects + nimgrects;
        nimgrects *= 2;
    }
    ir->filename = strdup(filename);
    ir->x = x;
    ir->y = y;
    ir->w = w;
    ir->h = h;
}

/* find_image_rectangle:
 * Find the image, if any, which contains a given point. Used for saving images
 * when they are clicked on. */
struct imgrect *find_image_rectangle(const int x, const int y) {
    struct imgrect *ir;
    for (ir = imgrects; ir < imgrects + nimgrects; ++ir)
        if (ir->filename && x >= ir->x && x < ir->x + ir->w && y >= ir->y && y < ir->y + ir->h)
            return ir;
    return NULL;
}

/* expose_event:
 * React to an expose event, perhaps changing the backing image size. */
void expose_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (darea) drawable = darea->window;
    gdk_drawable_get_size(GDK_DRAWABLE(drawable), &width, &height);
    if (!backing_image || backing_image->width != width || backing_image->height != height)
        make_backing_image();

    update_window();
}

/* configure_event:
 * React to a configure event, perhaps changing the backing image size. */
void configure_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (darea) drawable = darea->window;
    gdk_drawable_get_size(GDK_DRAWABLE(drawable), &width, &height);
    if (!backing_image || backing_image->width != width || backing_image->height != height)
        make_backing_image();

    update_window();
}

/* char *savedimgpfx:
 * The filename prefix with which we save images. */
char *savedimgpfx = "driftnet-";

/* save_image:
 * Save an image which the user has selected. */
void save_image(struct imgrect *ir) {
    static char *name;
    static int num;
    int fd1, fd2;
    char buf[8192];
    ssize_t l;
    struct stat st;

    if (!name)
        name = xcalloc(strlen(savedimgpfx) + 16, 1);

    do
        sprintf(name, "%s%d%s", savedimgpfx, num++, strrchr(ir->filename, '.'));
    while (stat(name, &st) == 0);
    log_msg(LOG_INFO, "saving `%s' as `%s'", ir->filename, name);

    fd1 = open(ir->filename, O_RDONLY);
    if (fd1 == -1) {
        log_msg(LOG_ERROR, "%s: %s", ir->filename, strerror(errno));
        return;
    }
    
    fd2 = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd2 == -1) {
        log_msg(LOG_ERROR, "%s: %s", name, strerror(errno));
        close(fd1);
        return;
    }

    /* XXX interrupts */
    while ((l = read(fd1, buf, sizeof buf)) > 0) {
        if (write(fd2, buf, l) == -1) {
            log_msg(LOG_ERROR, "%s: %s", name, strerror(errno));
            close(fd1);
            close(fd2);
            return;
        }
    }

    if (l == -1)
        log_msg(LOG_ERROR, "%s: %s", ir->filename, strerror(errno));

    close(fd1);
    close(fd2);
}

static struct {
    int x, y;
} click;

void button_press_event(GtkWidget *widget, GdkEventButton *event) {
    click.x = (int)event->x;
    click.y = (int)event->y;
}

void button_release_event(GtkWidget *widget, GdkEventButton *event) {
    struct imgrect *ir;
    ir = find_image_rectangle(click.x, click.y);
    if (ir && ir == find_image_rectangle((int)event->x, (int)event->y)) {
        /* We draw a little frame around the image while we're saving it, to
         * give some visual feedback. */
        gdk_draw_rectangle(drawable, darea->style->white_gc, 0, ir->x - 2, ir->y - 2, ir->w + 3, ir->h + 3);
        gdk_flush();    /* force X to actually draw the damn thing. */
        save_image(ir);
        xnanosleep(100000000);
        gdk_draw_rectangle(drawable, darea->style->black_gc, 0, ir->x - 2, ir->y - 2, ir->w + 3, ir->h + 3);
    }
}

void destroy(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

extern int dpychld_fd;  /* in driftnet.c */

/* xread:
 * Like read(2) but read the whole supplied length. */
static ssize_t xread(int fd, void *buf, size_t len) {
    char *p;
    for (p = (char*)buf; p < (char*)buf + len; ) {
        ssize_t l;
        l = read(fd, p, (char*)buf + len - p);
        if (l == -1 && errno != EINTR)
            return -1;
        else if (l == 0)
            return 0;
        else
            p += l;
    }
    return len;
}

gboolean pipe_event(GIOChannel chan, GIOCondition cond, gpointer data) {
    static char *path;
    char name[TMPNAMELEN];
    ssize_t rr;
    int nimgs = 0;

    if (!path)
        path = xmalloc(strlen(get_tmpdir()) + 34);

    /* We are sent messages of size TMPNAMELEN containing a null-terminated
     * file name. */
    while (nimgs < 4 && (rr = xread(dpychld_fd, name, sizeof name)) == sizeof name) {
        int saveimg = 0;
        struct stat st;

        ++nimgs;
        
        sprintf(path, "%s/%s", get_tmpdir(), name);

        if (stat(path, &st) == -1)
            continue;
           
        log_msg(LOG_INFO, "received image %s of size %d", name, (int)st.st_size);
        /* Check to see whether this looks like an image we're interested in. */
        if (st.st_size > 100) {
            /* Small images are probably bollocks. */
            img i = img_new();
            if (!img_load_file(i, path, header, unknown))
                log_msg(LOG_WARNING, "%s: bogus image (err = %d)", name, i->err);
            else {
                if (i->width > 8 && i->height > 8) {
                    if (img_load(i, full, i->type)) {
                        /* slot in the new image at some plausible place. */
                        int w, h;
                        if (i->width > width - 2 * BORDER) w = width - 2 * BORDER;
                        else w = i->width;
                        if (i->height > height - 2 * BORDER) h = height - 2 * BORDER;
                        else h = i->height;

                        /* is there space on this row? */
                        if (width - wrx < w) {
                            /* no */
                            scroll_backing_image(h + BORDER);
                            wrx = BORDER;
                            rowheight = h + BORDER;
                        }
                        if (rowheight < h + BORDER) {
                            scroll_backing_image(h + BORDER - rowheight);
                            rowheight = h + BORDER;
                        }

                        img_simple_blt(backing_image, wrx, wry - h, i, 0, 0, w, h);
                        add_image_rectangle(path, wrx, wry - h, w, h);
                        saveimg = 1;
			 
			/* XXX: remove get_options()->beep access; pass 'beep' in display start */
                        if (get_options()->beep)
                            write(1, "\a", 1);

                        update_window();

                        wrx += w + BORDER;
                    } else log_msg(LOG_WARNING, "%s: bogus image (err = %d)", name, i->err);
                } else log_msg(LOG_WARNING, "%s: image dimensions (%d x %d) too small to bother with", name, i->width, i->height);
            }

            img_delete(i);
        } else log_msg(LOG_WARNING, "image data too small (%d bytes) to bother with", (int)st.st_size);

        if (!saveimg)
            unlink(name);
    }
    if (rr == -1 && errno != EINTR && errno != EAGAIN) {
        perror(PROGNAME": read");
        gtk_main_quit();
    } else if (rr == 0) {
        /* pipe closed, exit. */
        gtk_main_quit();
    }
    return TRUE;
}

int dodisplay(int argc, char *argv[]) {
    GIOChannel *chan;
    struct imgrect *ir;

    /* have our main loop poll the pipe file descriptor */
    chan = g_io_channel_unix_new(dpychld_fd);
    g_io_add_watch(chan, G_IO_IN | G_IO_ERR | G_IO_HUP, (GIOFunc)pipe_event, NULL);
    fcntl(dpychld_fd, F_SETFL, O_NONBLOCK);

    /* set up list of image rectangles. */
    imgrects = xcalloc(nimgrects = 16, sizeof *imgrects);
       
    /* do some init thing */
    gtk_init(&argc, &argv);

    gtk_widget_push_colormap(gdk_rgb_get_colormap());

    /* Make our own window. */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request(window, 100, 100);

    darea = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window), darea);
    gtk_widget_set_events(darea, GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

    g_signal_connect(G_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(delete_event), NULL);
    g_signal_connect(G_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(destroy), NULL);

    g_signal_connect(G_OBJECT(darea), "expose-event", GTK_SIGNAL_FUNC(expose_event), NULL);
    g_signal_connect(G_OBJECT(darea), "configure_event", GTK_SIGNAL_FUNC(expose_event), NULL);
    
    /* mouse button press/release for saving images */
    g_signal_connect(G_OBJECT(darea), "button_press_event", GTK_SIGNAL_FUNC(button_press_event), NULL);
    g_signal_connect(G_OBJECT(darea), "button_press_event", GTK_SIGNAL_FUNC(button_release_event), NULL);

    gtk_widget_show_all(window);

    gtk_main();

    /* Get rid of all remaining images. */
    for (ir = imgrects; ir < imgrects + nimgrects; ++ir)
        if (ir->filename)
            unlink(ir->filename);

    img_delete(backing_image);
    
    gtk_exit(0);

    return 0; /* NOTREACHED */
}

#endif /* !NO_DISPLAY_WINDOW */
