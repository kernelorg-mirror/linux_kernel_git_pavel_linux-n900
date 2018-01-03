/*
 * Copyright (C) 2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _N9_H_
#define _N9_H_

struct dfl61audio_hsmic_event {
	void *private;
	void (*event)(void *priv, bool on);
};

void dfl61_jack_report(int status);
int dfl61_request_hsmicbias(bool enable);
void dfl61_register_hsmic_event_cb(struct dfl61audio_hsmic_event *event);
int dfl61_request_hp_enable(bool enable);
#endif
