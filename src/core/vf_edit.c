/* vf_edit.c - one-line editor. No I/O. */
#include <string.h>
#include "vf_edit.h"
#include "vf_util.h"

void vf_edit_init(vf_edit *e, const char *initial)
{
    memset(e, 0, sizeof *e);
    vf_strlcpy(e->buf, initial ? initial : "", sizeof e->buf);
    e->len = (int) strlen(e->buf);
    e->pos = e->len;
}

int vf_edit_key(vf_edit *e, int key)
{
    switch (key) {
    case VK_RETURN: case VK_ENTER: return VF_EDIT_DONE;
    case VK_F10: case VK_CTRL_Z: return VF_EDIT_CANCEL;
    case VK_LEFT: if (e->pos > 0) e->pos--; break;
    case VK_RIGHT: if (e->pos < e->len) e->pos++; break;
    case VK_CTRL_H: e->pos = 0; break;
    case VK_CTRL_E: e->pos = e->len; break;
    case VK_CTRL_A: e->overstrike = !e->overstrike; break;
    case VK_DEL:
        if (e->pos > 0) {
            memmove(e->buf + e->pos - 1, e->buf + e->pos, (size_t) (e->len - e->pos + 1));
            e->pos--; e->len--;
        }
        break;
    case VK_CTRL_U:
        memmove(e->buf, e->buf + e->pos, (size_t) (e->len - e->pos + 1));
        e->len -= e->pos; e->pos = 0;
        break;
    default:
        if (key >= 32 && key < 256 && key != VK_DEL) {
            if (e->overstrike && e->pos < e->len) {
                e->buf[e->pos++] = (char) key;
            } else if (e->len < (int) sizeof e->buf - 1) {
                memmove(e->buf + e->pos + 1, e->buf + e->pos, (size_t) (e->len - e->pos + 1));
                e->buf[e->pos++] = (char) key;
                e->len++;
            }
        }
    }
    return VF_EDIT_MORE;
}
