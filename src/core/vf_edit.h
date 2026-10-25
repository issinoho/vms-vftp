/* vf_edit.h - the one-line editor used by every prompt (DCL-style keys). */
#ifndef VF_EDIT_H
#define VF_EDIT_H

#include "vf_types.h"

typedef struct {
    char buf[VF_SPEC_MAX];
    int len, pos;
    int overstrike;              /* Ctrl/A toggles, as in DCL */
} vf_edit;

enum { VF_EDIT_MORE, VF_EDIT_DONE, VF_EDIT_CANCEL };

void vf_edit_init(vf_edit *e, const char *initial);
/* Feed one key. Return toggles done; F10 and Ctrl/Z cancel.
   Left/right, Ctrl/H (start), Ctrl/E (end), Delete (rub out), Ctrl/U (erase to start),
   Ctrl/A (insert/overstrike). */
int vf_edit_key(vf_edit *e, int key);

#endif
