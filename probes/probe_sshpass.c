/* probe_sshpass.c <host> <port> <user> <passfile> - how an SSH server takes a
   password: a fresh session for the "password" method, then one for
   keyboard-interactive, printing each result, libssh2's error and the server's
   prompts (never the password, read from the first line of passfile). The host key
   is not checked: localhost only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <libssh2.h>

static char pass[256];
static int rounds;

static int dial(const char *host, const char *port)
{
    struct addrinfo hints, *res = 0;
    int fd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) || !res) return -1;
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) < 0) { close(fd); fd = -1; }
    freeaddrinfo(res);
    return fd;
}

static void err(const char *what, LIBSSH2_SESSION *s, int rc)
{
    char *m = 0;
    int len = 0;
    libssh2_session_last_error(s, &m, &len, 0);
    printf("SSHPASS: %s rc=%d last_errno=%d \"%s\"\n", what, rc, libssh2_session_last_errno(s), m ? m : "");
}

static void kbd(const char *name, int name_len, const char *instr, int instr_len, int n,
                const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts, LIBSSH2_USERAUTH_KBDINT_RESPONSE *resp,
                void **abstract)
{
    int i;
    size_t pl = strlen(pass);
    (void) abstract;
    rounds++;
    printf("SSHPASS: kbd round %d name \"%.*s\" instruction \"%.*s\" prompts %d\n",
           rounds, name_len, name ? name : "", instr_len, instr ? instr : "", n);
    for (i = 0; i < n; i++) {
        printf("SSHPASS:   prompt %d \"%.*s\" echo %d\n", i, (int) prompts[i].length,
               (const char *) prompts[i].text, (int) prompts[i].echo);
        resp[i].text = malloc(pl + 1);
        if (resp[i].text) { memcpy(resp[i].text, pass, pl + 1); resp[i].length = (unsigned int) pl; }
    }
}

static LIBSSH2_SESSION *open_session(const char *host, const char *port, const char *user, int *fd)
{
    LIBSSH2_SESSION *s;
    char *m;
    int rc;
    *fd = dial(host, port);
    if (*fd < 0) { printf("SSHPASS: cannot connect\n"); return 0; }
    s = libssh2_session_init();
    libssh2_session_set_blocking(s, 1);
    libssh2_session_set_timeout(s, 60000);
    rc = libssh2_session_handshake(s, *fd);
    if (rc) { err("handshake", s, rc); return s; }
    m = libssh2_userauth_list(s, user, (unsigned int) strlen(user));
    printf("SSHPASS: methods %s\n", m ? m : "(none)");
    return s;
}

static void done(LIBSSH2_SESSION *s, int fd)
{
    if (s) { libssh2_session_disconnect(s, "probe done"); libssh2_session_free(s); }
    if (fd >= 0) close(fd);
}

int main(int argc, char **argv)
{
    LIBSSH2_SESSION *s;
    FILE *f;
    int fd, rc;
    if (argc < 5) { printf("usage: probe_sshpass host port user passfile\n"); return 1; }
    f = fopen(argv[4], "r");
    if (!f || !fgets(pass, sizeof pass, f)) { printf("SSHPASS: no password file\n"); return 1; }
    fclose(f);
    pass[strcspn(pass, "\r\n")] = '\0';
    libssh2_init(0);

    s = open_session(argv[1], argv[2], argv[3], &fd);
    if (s) {
        rc = libssh2_userauth_password(s, argv[3], pass);
        if (rc) err("password method", s, rc); else printf("SSHPASS: password method: logged in\n");
    }
    done(s, fd);

    s = open_session(argv[1], argv[2], argv[3], &fd);
    if (s) {
        rc = libssh2_userauth_keyboard_interactive(s, argv[3], kbd);
        if (rc) err("keyboard-interactive", s, rc); else printf("SSHPASS: keyboard-interactive: logged in\n");
    }
    done(s, fd);
    memset(pass, 0, sizeof pass);
    libssh2_exit();
    return 0;
}
