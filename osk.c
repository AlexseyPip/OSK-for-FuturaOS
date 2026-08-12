/**
 * @brief On-screen keyboard 
 *
 * @copyright
 * This file is part of FuturaOS and is released under the terms
 * of the MIT License
 *
 * Version: 1.0.0
 * Last update: 12.08.2026
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define OSK_ROWS 7
#define OSK_PAD 3
#define OSK_MARGIN 4
#define OSK_LOCK_PATH "/tmp/osk.lock"

struct OskKey {
	const char * label;
	kbd_key_t keycode;
	unsigned char ascii;
	float width_units;
	int sticky; /* 1 = latch (Shift/Ctrl/Alt/Caps) */
};

static yutani_t * yctx;
static yutani_window_t * window;
static gfx_context_t * ctx;
static struct TT_Font * font;
static int should_exit = 0;
static int shift_on = 0;
static int ctrl_on = 0;
static int alt_on = 0;
static int caps_on = 0;
static int pressed = -1;

/* Full layout: function row, numbers, QWERTY, home cluster, modifiers. */
static const struct OskKey row0[] = {
	{"Esc", KEY_ESCAPE, 27, 1.2f, 0},
	{"F1", KEY_F1, 0, 1.0f, 0}, {"F2", KEY_F2, 0, 1.0f, 0}, {"F3", KEY_F3, 0, 1.0f, 0},
	{"F4", KEY_F4, 0, 1.0f, 0}, {"F5", KEY_F5, 0, 1.0f, 0}, {"F6", KEY_F6, 0, 1.0f, 0},
	{"F7", KEY_F7, 0, 1.0f, 0}, {"F8", KEY_F8, 0, 1.0f, 0}, {"F9", KEY_F9, 0, 1.0f, 0},
	{"F10", KEY_F10, 0, 1.0f, 0}, {"F11", KEY_F11, 0, 1.0f, 0}, {"F12", KEY_F12, 0, 1.0f, 0},
};
static const struct OskKey row1[] = {
	{"`", '`', '`', 1.0f, 0}, {"1", '1', '1', 1.0f, 0}, {"2", '2', '2', 1.0f, 0},
	{"3", '3', '3', 1.0f, 0}, {"4", '4', '4', 1.0f, 0}, {"5", '5', '5', 1.0f, 0},
	{"6", '6', '6', 1.0f, 0}, {"7", '7', '7', 1.0f, 0}, {"8", '8', '8', 1.0f, 0},
	{"9", '9', '9', 1.0f, 0}, {"0", '0', '0', 1.0f, 0}, {"-", '-', '-', 1.0f, 0},
	{"=", '=', '=', 1.0f, 0}, {"Bksp", KEY_BACKSPACE, '\b', 2.0f, 0},
};
static const struct OskKey row2[] = {
	{"Tab", '\t', '\t', 1.5f, 0}, {"q", 'q', 'q', 1.0f, 0}, {"w", 'w', 'w', 1.0f, 0},
	{"e", 'e', 'e', 1.0f, 0}, {"r", 'r', 'r', 1.0f, 0}, {"t", 't', 't', 1.0f, 0},
	{"y", 'y', 'y', 1.0f, 0}, {"u", 'u', 'u', 1.0f, 0}, {"i", 'i', 'i', 1.0f, 0},
	{"o", 'o', 'o', 1.0f, 0}, {"p", 'p', 'p', 1.0f, 0}, {"[", '[', '[', 1.0f, 0},
	{"]", ']', ']', 1.0f, 0}, {"\\", '\\', '\\', 1.5f, 0},
};
static const struct OskKey row3[] = {
	{"Caps", KEY_NONE, 0, 1.75f, 1}, {"a", 'a', 'a', 1.0f, 0}, {"s", 's', 's', 1.0f, 0},
	{"d", 'd', 'd', 1.0f, 0}, {"f", 'f', 'f', 1.0f, 0}, {"g", 'g', 'g', 1.0f, 0},
	{"h", 'h', 'h', 1.0f, 0}, {"j", 'j', 'j', 1.0f, 0}, {"k", 'k', 'k', 1.0f, 0},
	{"l", 'l', 'l', 1.0f, 0}, {";", ';', ';', 1.0f, 0}, {"'", '\'', '\'', 1.0f, 0},
	{"Enter", '\n', '\n', 2.25f, 0},
};
static const struct OskKey row4[] = {
	{"Shift", KEY_LEFT_SHIFT, 0, 2.25f, 1}, {"z", 'z', 'z', 1.0f, 0}, {"x", 'x', 'x', 1.0f, 0},
	{"c", 'c', 'c', 1.0f, 0}, {"v", 'v', 'v', 1.0f, 0}, {"b", 'b', 'b', 1.0f, 0},
	{"n", 'n', 'n', 1.0f, 0}, {"m", 'm', 'm', 1.0f, 0}, {",", ',', ',', 1.0f, 0},
	{".", '.', '.', 1.0f, 0}, {"/", '/', '/', 1.0f, 0}, {"Shift", KEY_LEFT_SHIFT, 0, 2.75f, 1},
};
static const struct OskKey row5[] = {
	{"Ctrl", KEY_LEFT_CTRL, 0, 1.5f, 1}, {"Alt", KEY_LEFT_ALT, 0, 1.5f, 1},
	{"Space", ' ', ' ', 6.5f, 0},
	{"←", KEY_ARROW_LEFT, 0, 1.1f, 0}, {"↓", KEY_ARROW_DOWN, 0, 1.1f, 0},
	{"↑", KEY_ARROW_UP, 0, 1.1f, 0}, {"→", KEY_ARROW_RIGHT, 0, 1.1f, 0},
};
static const struct OskKey row6[] = {
	{"Ins", KEY_INSERT, 0, 1.2f, 0}, {"Home", KEY_HOME, 0, 1.2f, 0},
	{"PgUp", KEY_PAGE_UP, 0, 1.2f, 0}, {"Del", KEY_DEL, 0, 1.2f, 0},
	{"End", KEY_END, 0, 1.2f, 0}, {"PgDn", KEY_PAGE_DOWN, 0, 1.2f, 0},
	{"PrtSc", KEY_PRINT_SCREEN, 0, 1.3f, 0}, {"ScrLk", KEY_SCROLL_LOCK, 0, 1.3f, 0},
	{"Pause", KEY_PAUSE, 0, 1.3f, 0},
};

static const struct OskKey * rows[OSK_ROWS] = {
	row0, row1, row2, row3, row4, row5, row6
};
static const int row_lens[OSK_ROWS] = {
	(int)(sizeof(row0) / sizeof(row0[0])),
	(int)(sizeof(row1) / sizeof(row1[0])),
	(int)(sizeof(row2) / sizeof(row2[0])),
	(int)(sizeof(row3) / sizeof(row3[0])),
	(int)(sizeof(row4) / sizeof(row4[0])),
	(int)(sizeof(row5) / sizeof(row5[0])),
	(int)(sizeof(row6) / sizeof(row6[0])),
};

static float row_units(int r) {
	float u = 0;
	for (int i = 0; i < row_lens[r]; ++i)
		u += rows[r][i].width_units;
	return u;
}

static int flat_index(int r, int c) {
	int n = 0;
	for (int i = 0; i < r; ++i)
		n += row_lens[i];
	return n + c;
}

static void key_at(int flat, int * out_r, int * out_c) {
	int n = 0;
	for (int r = 0; r < OSK_ROWS; ++r) {
		if (flat < n + row_lens[r]) {
			*out_r = r;
			*out_c = flat - n;
			return;
		}
		n += row_lens[r];
	}
	*out_r = -1;
	*out_c = -1;
}

static kbd_mod_t current_mods(void) {
	kbd_mod_t m = 0;
	if (shift_on || caps_on) m |= KEY_MOD_LEFT_SHIFT;
	if (ctrl_on) m |= KEY_MOD_LEFT_CTRL;
	if (alt_on) m |= KEY_MOD_LEFT_ALT;
	return m;
}

static int letters_shifted(void) {
	/* Caps XOR Shift for letters */
	return (caps_on && !shift_on) || (!caps_on && shift_on);
}

static void inject_key(kbd_key_t keycode, unsigned char ascii, kbd_act_t action) {
	key_event_t ev;
	key_event_state_t st;
	memset(&ev, 0, sizeof(ev));
	memset(&st, 0, sizeof(st));
	ev.keycode = keycode;
	ev.action = action;
	ev.modifiers = current_mods();
	if (action == KEY_ACTION_DOWN)
		ev.key = ascii;
	else
		ev.key = 0;
	yutani_msg_buildx_key_event_alloc(m);
	yutani_msg_buildx_key_event(m, 0, &ev, &st);
	yutani_msg_send(yctx, m);
}

static unsigned char shifted_ascii(unsigned char c) {
	if (c >= 'a' && c <= 'z')
		return (unsigned char)(c - 'a' + 'A');
	switch (c) {
		case '1': return '!';
		case '2': return '@';
		case '3': return '#';
		case '4': return '$';
		case '5': return '%';
		case '6': return '^';
		case '7': return '&';
		case '8': return '*';
		case '9': return '(';
		case '0': return ')';
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case '`': return '~';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		default: return c;
	}
}

static int key_is_lit(const struct OskKey * k) {
	if (!strcmp(k->label, "Shift")) return shift_on;
	if (!strcmp(k->label, "Caps")) return caps_on;
	if (!strcmp(k->label, "Ctrl")) return ctrl_on;
	if (!strcmp(k->label, "Alt")) return alt_on;
	return 0;
}

static void press_key(const struct OskKey * k) {
	if (k->sticky) {
		if (!strcmp(k->label, "Shift")) {
			shift_on = !shift_on;
			return;
		}
		if (!strcmp(k->label, "Caps")) {
			caps_on = !caps_on;
			return;
		}
		if (!strcmp(k->label, "Ctrl")) {
			ctrl_on = !ctrl_on;
			return;
		}
		if (!strcmp(k->label, "Alt")) {
			alt_on = !alt_on;
			return;
		}
	}

	kbd_key_t code = k->keycode;
	unsigned char ch = k->ascii;
	if (ch && ch >= 'a' && ch <= 'z') {
		if (letters_shifted())
			ch = (unsigned char)(ch - 'a' + 'A');
	} else if (ch && shift_on) {
		ch = shifted_ascii(ch);
	}
	if (!code)
		code = (kbd_key_t)ch;

	inject_key(code, ch, KEY_ACTION_DOWN);
	inject_key(code, ch, KEY_ACTION_UP);
}

static int hit_test(int mx, int my, int * out_r, int * out_c) {
	int w = (int)window->width;
	int h = (int)window->height;
	int row_h = (h - OSK_MARGIN * 2) / OSK_ROWS;
	if (my < OSK_MARGIN || my >= h - OSK_MARGIN)
		return 0;
	int r = (my - OSK_MARGIN) / row_h;
	if (r < 0 || r >= OSK_ROWS)
		return 0;
	float units = row_units(r);
	float unit_w = (float)(w - OSK_MARGIN * 2) / units;
	float x = (float)OSK_MARGIN;
	for (int c = 0; c < row_lens[r]; ++c) {
		float kw = rows[r][c].width_units * unit_w;
		if (mx >= (int)x && mx < (int)(x + kw)) {
			*out_r = r;
			*out_c = c;
			return 1;
		}
		x += kw;
	}
	return 0;
}

static void redraw(void) {
	int w = (int)window->width;
	int h = (int)window->height;
	draw_fill(ctx, rgba(28, 32, 40, 235));
	draw_rectangle(ctx, 0, 0, (uint16_t)w, 1, rgb(80, 90, 110));

	int row_h = (h - OSK_MARGIN * 2) / OSK_ROWS;
	float font_px = row_h > 28 ? 13.0f : 11.0f;
	tt_set_size_px(font, font_px);

	for (int r = 0; r < OSK_ROWS; ++r) {
		float units = row_units(r);
		float unit_w = (float)(w - OSK_MARGIN * 2) / units;
		float x = (float)OSK_MARGIN;
		int y = OSK_MARGIN + r * row_h;
		for (int c = 0; c < row_lens[r]; ++c) {
			const struct OskKey * k = &rows[r][c];
			int kw = (int)(k->width_units * unit_w) - OSK_PAD;
			int kh = row_h - OSK_PAD;
			int kx = (int)x + OSK_PAD / 2;
			int ky = y + OSK_PAD / 2;
			int fi = flat_index(r, c);
			int lit = (fi == pressed) || key_is_lit(k);
			uint32_t bg = lit ? rgb(70, 120, 200) : rgb(48, 54, 66);
			uint32_t fg = rgb(235, 238, 245);
			if (kw < 4) kw = 4;
			if (kh < 4) kh = 4;
			draw_rounded_rectangle(ctx, kx, ky, (uint16_t)kw, (uint16_t)kh, 3, bg);

			char label[16];
			if (k->ascii && k->ascii >= 'a' && k->ascii <= 'z' && letters_shifted()) {
				label[0] = (char)(k->ascii - 'a' + 'A');
				label[1] = '\0';
			} else if (k->ascii && shift_on && k->ascii > 32 && strlen(k->label) == 1) {
				label[0] = (char)shifted_ascii(k->ascii);
				label[1] = '\0';
			} else {
				snprintf(label, sizeof(label), "%s", k->label);
			}
			int tw = tt_string_width(font, label);
			int tx = kx + (kw - tw) / 2;
			int ty = ky + kh / 2 + (int)(font_px * 0.35f);
			if (tx < kx + 1) tx = kx + 1;
			tt_draw_string(ctx, font, tx, ty, label, fg);
			x += k->width_units * unit_w;
		}
	}
	flip(ctx);
	yutani_flip(yctx, window);
}

static void place_window(void) {
	int dw = (int)yctx->display_width;
	int dh = (int)yctx->display_height;
	yutani_window_move(yctx, window, 0, dh - (int)window->height);
	yutani_set_stack(yctx, window, YUTANI_ZORDER_OVERLAY);
	(void)dw;
}

static int acquire_singleton(void) {
	int fd = open(OSK_LOCK_PATH, O_CREAT | O_WRONLY | O_EXCL, 0644);
	if (fd >= 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
		write(fd, buf, strlen(buf));
		close(fd);
		return 0;
	}
	if (errno == EEXIST) {
		fprintf(stderr, "osk: already running (%s)\n", OSK_LOCK_PATH);
		return -1;
	}
	/* If /tmp missing exclusivity, continue anyway. */
	return 0;
}

int main(int argc, char ** argv) {
	(void)argc;
	(void)argv;

	if (acquire_singleton() < 0)
		return 0;

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "osk: no compositor\n");
		unlink(OSK_LOCK_PATH);
		return 1;
	}

	font = tt_font_from_shm("sans-serif");
	if (!font)
		font = tt_font_from_shm("monospace");
	if (!font) {
		fprintf(stderr, "osk: no font\n");
		unlink(OSK_LOCK_PATH);
		return 1;
	}

	int dw = (int)yctx->display_width;
	int dh = (int)yctx->display_height;
	int kh = dh * 42 / 100;
	if (kh < 220) kh = 220;
	if (kh > 360) kh = 360;

	window = yutani_window_create_flags(yctx, dw, kh,
		YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS |
		YUTANI_WINDOW_FLAG_DISALLOW_DRAG |
		YUTANI_WINDOW_FLAG_DISALLOW_RESIZE |
		YUTANI_WINDOW_FLAG_NO_ANIMATION);
	if (!window) {
		fprintf(stderr, "osk: window create failed\n");
		unlink(OSK_LOCK_PATH);
		return 1;
	}

	yutani_window_advertise(yctx, window, "On-Screen Keyboard");
	place_window();
	ctx = init_graphics_yutani_double_buffer(window);
	redraw();

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (!m)
			continue;
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT: {
				struct yutani_msg_window_mouse_event * me = (void *)m->data;
				if (me->wid != window->wid)
					break;
				if (me->command == YUTANI_MOUSE_EVENT_DOWN &&
				    (me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
					int r, c;
					if (hit_test(me->new_x, me->new_y, &r, &c)) {
						pressed = flat_index(r, c);
						redraw();
					}
				} else if (me->command == YUTANI_MOUSE_EVENT_RAISE ||
				           me->command == YUTANI_MOUSE_EVENT_CLICK) {
					if (pressed >= 0) {
						int r, c;
						key_at(pressed, &r, &c);
						if (r >= 0)
							press_key(&rows[r][c]);
						pressed = -1;
						yutani_set_stack(yctx, window, YUTANI_ZORDER_OVERLAY);
						redraw();
					}
				} else if (me->command == YUTANI_MOUSE_EVENT_LEAVE) {
					if (pressed >= 0) {
						pressed = -1;
						redraw();
					}
				}
				break;
			}
			case YUTANI_MSG_WINDOW_CLOSE:
			case YUTANI_MSG_SESSION_END:
				should_exit = 1;
				break;
			case YUTANI_MSG_WELCOME:
				place_window();
				redraw();
				break;
			default:
				break;
		}
		free(m);
	}

	yutani_close(yctx, window);
	unlink(OSK_LOCK_PATH);
	return 0;
}
