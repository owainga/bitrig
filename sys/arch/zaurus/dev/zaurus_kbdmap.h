/* $OpenBSD: zaurus_kbdmap.h,v 1.14 2005/01/18 01:03:15 drahn Exp $ */

/*
 * Copyright (c) 2005 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define KC(n) KS_KEYCODE(n)

static const keysym_t zkbd_keydesc_us[] = {
    KC(0),	KS_Control_L,
    KC(2),	KS_Tab,		KS_Tab,		KS_Caps_Lock,
    KC(3),	KS_Cmd_Screen1,	KS_f2,				/* Addr, */
    KC(4),	KS_Cmd_Screen0,	KS_f1,				/* Cal, */
    KC(5),	KS_Cmd_Screen2,	KS_f3,				/* Mail, */
    KC(6),	KS_Cmd_Screen3,	KS_f4,				/* Home, */
    KC(8),	KS_1,		KS_exclam,
    KC(9),	KS_2,		KS_quotedbl,
    KC(10),	KS_q,
    KC(11),	KS_w,		KS_W,		KS_asciicircum,
    KC(12),	KS_a,
    KC(13),	KS_z,
    KC(14),	KS_Cmd,		KS_Alt_L,
    KC(16),	KS_Cmd_BrightnessUp,	KS_3,	KS_numbersign,
    KC(17),	KS_Cmd_BrightnessDown,	KS_4,	KS_dollar,	
    KC(18),	KS_e,		KS_E,		KS_equal,
    KC(19),	KS_s,
    KC(20),	KS_d,		KS_D,		KS_grave,
    KC(21),	KS_x,
    /* KC(22),	^/t (right japanese) */
    KC(24),	KS_5,		KS_percent,
    KC(25),	KS_r,		KS_R,		KS_plus,
    KC(26),	KS_t,		KS_T,		KS_bracketleft,
    KC(27),	KS_f,		KS_F,		KS_backslash,
    KC(28),	KS_c,
    KC(29),	KS_minus,	KS_minus,	KS_at,
    KC(30),	KS_Escape, /* Cancel */
    KC(32),	KS_6,		KS_ampersand,
    KC(33),	KS_y,		KS_Y,		KS_bracketright,
    KC(34),	KS_g,		KS_G,		KS_semicolon,
    KC(35),	KS_v,
    KC(36),	KS_b,		KS_B,		KS_underscore,
    KC(37),	KS_space,
    KC(38),	KS_KP_Enter,	/* ok */
    KC(40),	KS_7,		KS_apostrophe,
    KC(41),	KS_8,		KS_parenleft,
    KC(42),	KS_u,		KS_U,		KS_braceleft,	
    KC(43),	KS_h,		KS_H,		KS_colon,
    KC(44),	KS_n,
    KC(45),	KS_comma,	KS_slash,	KS_less,
    KC(46),	KS_Cmd_Screen4,	KS_f5,				/* Menu, */
    KC(48),	KS_9,		KS_parenright,
    KC(49),	KS_i,		KS_I,		KS_braceright,
    KC(50),	KS_j,		KS_J,		KS_asterisk,
    KC(51),	KS_m,
    KC(52),	KS_period,	KS_question,	KS_greater,
    KC(54),	KS_KP_Left, /* left, */
    KC(56),	KS_0,		KS_asciitilde,
    KC(57),	KS_o,
    KC(58),	KS_k,
    KC(59),	KS_l,		KS_L,		KS_bar,
    KC(61),	KS_KP_Up, /* up, */
    KC(62),	KS_KP_Down, /* down, */
    KC(64),	KS_Delete,	KS_BackSpace,
    KC(65),	KS_p,
    KC(68),	KS_Return,
    KC(70),	KS_KP_Right, /* right, */
    KC(80),	KS_KP_Right, /* OK, (ext) */
    KC(81),	KS_KP_Down, /* tog left, */
    KC(83),	KS_Shift_R,
    KC(84),	KS_Shift_L,
    KC(88),	KS_KP_Left, /* cancel (ext), */
    KC(89),	KS_KP_Up, /* tog right, */
    KC(93),	KS_Mode_switch /* Fn */
};

#ifdef WSDISPLAY_COMPAT_RAWKBD
static const char xt_keymap[] = {
    /* KC(0), */	86,/* KS_Control_L, */
    /* KC(1), */	 0, /* NC */
    /* KC(2), */	15, /* KS_Tab,	KS_Tab,		KS_Caps_Lock, */
    /* KC(3), */	60, /* KS_Cmd_Screen1,	KS_f2,		Addr, */
    /* KC(4), */	59, /* KS_Cmd_Screen0,	KS_f1,		Cal, */
    /* KC(5), */	61, /* KS_Cmd_Screen2,	KS_f3,		Mail, */
    /* KC(6), */	62, /* KS_Cmd_Screen3,	KS_f4,		Home, */
    /* KC(7), */	 0, /* NC */
    /* KC(8), */	 2, /* KS_1,		KS_exclam, */
    /* KC(9), */	 3, /* KS_2,		KS_quotedbl, */
    /* KC(10), */	16, /* KS_q, */
    /* KC(11), */	17, /* KS_w,	KS_W,		KS_asciicircum, */
    /* KC(12), */	30, /* KS_a, */
    /* KC(13), */	44, /* KS_z, */
    /* KC(14), */	56, /* KS_Cmd,		KS_Alt_L, */
    /* KC(15), */	 0, /* NC */
    /* KC(16), */	 4, /* KS_3,		KS_numbersign, */
    /* KC(17), */	 5, /* KS_4,		KS_dollar, */
    /* KC(18), */	18, /* KS_e,		KS_E,		KS_equal, */
    /* KC(19), */	31, /* KS_s, */
    /* KC(20), */	32, /* KS_d,		KS_D,		KS_grave, */
    /* KC(21), */	45, /* KS_x, */
    /* KC(22), */	 0, /* ^/t (right japanese) */
    /* KC(23), */	 0, /* NC */
    /* KC(24), */	 6, /* KS_5,		KS_percent, */
    /* KC(25), */	19, /* KS_r,		KS_R,		KS_plus, */
    /* KC(26), */	20, /* KS_t,	KS_T,		KS_bracketleft, */
    /* KC(27), */	33, /* KS_f,		KS_F,		KS_backslash, */
    /* KC(28), */	46, /* KS_c, */
    /* KC(29), */	74, /* KS_minus,	KS_minus,	KS_at, */
    /* KC(30), */	 1, /* KS_Escape, Cancel */
    /* KC(31), */	 0, /* NC */
    /* KC(32), */	 7, /* KS_6,		KS_ampersand, */
    /* KC(33), */	21, /* KS_y,	KS_Y,		KS_bracketright, */
    /* KC(34), */	34, /* KS_g,		KS_G,		KS_semicolon, */
    /* KC(35), */	47, /* KS_v, */
    /* KC(36), */	48, /* KS_b,	KS_B,		KS_underscore, */
    /* KC(37), */	57, /* KS_space, */
    /* KC(38), */	 0, /* KS_KP_Enter,	ok */
    /* KC(39), */	 0, /* NC */
    /* KC(40), */	 8, /* KS_7,	KS_apostrophe, */
    /* KC(41), */	 9, /* KS_8,	KS_parenleft, */
    /* KC(42), */	22, /* KS_u,	KS_U,		KS_braceleft,	 */
    /* KC(43), */	35, /* KS_h,	KS_H,		KS_colon, */
    /* KC(44), */	49, /* KS_n, */
    /* KC(45), */	51, /* KS_comma,	KS_slash,	KS_less, */
    /* KC(46), */	63, /* KS_Cmd_Screen4,	KS_f5,		Menu, */
    /* KC(47), */	 0, /* NC */
    /* KC(48), */	10, /* KS_9,	KS_parenright, */
    /* KC(49), */	23, /* KS_i,	KS_I,		KS_braceright, */
    /* KC(50), */	36, /* KS_j,	KS_J,		KS_asterisk, */
    /* KC(51), */	50, /* KS_m, */
    /* KC(52), */	52, /* KS_period,	KS_question,	KS_greater, */
    /* KC(53), */	 0, /* NC */
    /* KC(54), */	203, /* KS_KP_Left, left, */
    /* KC(55), */	 0, /* NC */
    /* KC(56), */	11, /* KS_0,		KS_asciitilde, */
    /* KC(57), */	24, /* KS_o, */
    /* KC(58), */	37, /* KS_k, */
    /* KC(59), */	38, /* KS_l,		KS_L,		KS_bar, */
    /* KC(60), */	 0, /* NC */
    /* KC(61), */	200, /* KS_KP_Up, up, */
    /* KC(62), */	208, /* KS_KP_Down, down, */
    /* KC(63), */	 0, /* NC */
    /* KC(64), */	221, /* KS_Delete,	KS_BackSpace, */
    /* KC(65), */	255, /* KS_p, */
    /* KC(66), */	 0, /* NC */
    /* KC(67), */	 0, /* NC */
    /* KC(68), */	156, /* KS_Return, */
    /* KC(69), */	 0, /* NC */
    /* KC(70), */	205, /* KS_KP_Right, right, */
    /* KC(71), */	 0, /* NC */
    /* KC(72), */	 0, /* NC */
    /* KC(73), */	 0, /* NC */
    /* KC(74), */	 0, /* NC */
    /* KC(75), */	 0, /* NC */
    /* KC(76), */	 0, /* NC */
    /* KC(77), */	 0, /* NC */
    /* KC(78), */	 0, /* NC */
    /* KC(79), */	 0, /* NC */
    /* KC(80), */	205, /* KS_KP_Right, OK, (ext) */
    /* KC(81), */	208, /* KS_KP_Down, tog left, */
    /* KC(82), */	 0, /* NC */
    /* KC(83), */	0, /* KS_Shift_R, */
    /* KC(84), */	0, /* KS_Shift_L, */
    /* KC(85), */	 0, /* NC */
    /* KC(86), */	 0, /* NC */
    /* KC(87), */	 0, /* NC */
    /* KC(88), */	203, /* KS_KP_Left, cancel (ext), */
    /* KC(89), */	200, /* KS_KP_Up, tog right, */
    /* KC(90), */	 0, /* NC */
    /* KC(91), */	 0, /* NC */
    /* KC(92), */	 0, /* NC */
    /* KC(93), */	56, /* KS_Mode_switch Fn */
};
#endif

#define KBD_MAP(name, base, map) \
			{ name, base, sizeof(map)/sizeof(keysym_t), map }

static const struct wscons_keydesc zkbd_keydesctab[] = {
        KBD_MAP(KB_US,                  0,      zkbd_keydesc_us),
        {0, 0, 0, 0}
};

#undef KBD_MAP
#undef KC
