#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <3ds.h>
#include <jansson.h>

// ─── Configuration ────────────────────────────────────────────────────────────
#define APP_NAME         "3DSMate"
#define GROQ_HOST        "api.groq.com"
#define GROQ_PATH        "/openai/v1/chat/completions"
#define GROQ_MODEL       "openai/gpt-oss-120b"
#define MAX_INPUT        512
#define MAX_RESPONSE     4096
#define MAX_HISTORY      30
#define MAX_CHATS        20
#define SCREEN_WIDTH     49

#define DATA_DIR         "sdmc:/3ds/3dsmate"
#define CONFIG_FILE      "sdmc:/3ds/3dsmate/config.ini"
#define CHATS_DIR        "sdmc:/3ds/3dsmate/chats"

// ─── Colors ───────────────────────────────────────────────────────────────────
#define C_RESET   "\x1b[0m"
#define C_BOLD    "\x1b[1m"
#define C_WHITE   "\x1b[37m"
#define C_CYAN    "\x1b[36m"
#define C_GREEN   "\x1b[32m"
#define C_YELLOW  "\x1b[33m"
#define C_RED     "\x1b[31m"
#define C_MAGENTA "\x1b[35m"

// ─── App States ───────────────────────────────────────────────────────────────
typedef enum {
    STATE_MAIN_MENU,
    STATE_CHAT,
    STATE_CHAT_LIST
} AppState;

// ─── Structs ──────────────────────────────────────────────────────────────────
typedef struct {
    char role[16];
    char content[MAX_RESPONSE];
} Message;

typedef struct {
    char    title[64];
    char    filename[128];
    Message messages[MAX_HISTORY];
    int     count;
} Chat;

typedef struct {
    char     api_key[200];
    Chat     current_chat;
    char     chat_files[MAX_CHATS][128];
    char     chat_titles[MAX_CHATS][64];
    int      chat_count;
    int      selected_chat;
    AppState state;
    char     status_msg[128];
    bool     status_ok;
} AppCtx;

// ─── Utility ──────────────────────────────────────────────────────────────────

void ensure_dirs(void) {
    mkdir(DATA_DIR, 0777);
    mkdir(CHATS_DIR, 0777);
}

void draw_header(const char *title) {
    consoleClear();
    printf(C_CYAN C_BOLD);
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║  🤖 %-38s║\n", title);
    printf("╚═══════════════════════════════════════════╝\n" C_RESET);
}

void set_status(AppCtx *app, const char *msg, bool ok) {
    strncpy(app->status_msg, msg, sizeof(app->status_msg) - 1);
    app->status_ok = ok;
}

void draw_status(AppCtx *app) {
    if (strlen(app->status_msg) > 0)
        printf("%s » %s" C_RESET "\n", app->status_ok ? C_GREEN : C_RED, app->status_msg);
}

void print_wrapped(const char *text, const char *color, int indent) {
    int len = strlen(text), pos = 0;
    printf("%s", color);
    while (pos < len) {
        int avail = SCREEN_WIDTH - indent;
        int chunk = (len - pos > avail) ? avail : (len - pos);
        if (chunk < len - pos)
            for (int i = chunk - 1; i >= 0; i--)
                if (text[pos + i] == ' ') { chunk = i + 1; break; }
        for (int i = 0; i < indent; i++) printf(" ");
        printf("%.*s\n", chunk, text + pos);
        pos += chunk;
        if (pos < len && text[pos] == ' ') pos++;
    }
    printf(C_RESET);
}

// ─── API Key Persistence ──────────────────────────────────────────────────────

void save_api_key(const char *key) {
    ensure_dirs();
    FILE *f = fopen(CONFIG_FILE, "w");
    if (f) { fprintf(f, "api_key=%s\n", key); fclose(f); }
}

bool load_api_key(char *key, size_t max) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "api_key=", 8) == 0) {
            size_t len = strlen(line + 8);
            if (len > 0 && line[8 + len - 1] == '\n') line[8 + len - 1] = '\0';
            strncpy(key, line + 8, max - 1);
            fclose(f);
            return strlen(key) > 5;
        }
    }
    fclose(f);
    return false;
}

// ─── Chat Persistence ─────────────────────────────────────────────────────────

void save_chat(Chat *chat) {
    if (strlen(chat->filename) == 0) return;
    ensure_dirs();
    json_t *root = json_object();
    json_t *msgs = json_array();
    json_object_set_new(root, "title", json_string(chat->title));
    for (int i = 0; i < chat->count; i++) {
        json_t *m = json_object();
        json_object_set_new(m, "role",    json_string(chat->messages[i].role));
        json_object_set_new(m, "content", json_string(chat->messages[i].content));
        json_array_append_new(msgs, m);
    }
    json_object_set_new(root, "messages", msgs);
    char path[200];
    snprintf(path, sizeof(path), "%s/%s", CHATS_DIR, chat->filename);
    json_dump_file(root, path, JSON_INDENT(2));
    json_decref(root);
}

bool load_chat(Chat *chat, const char *filename) {
    char path[200];
    snprintf(path, sizeof(path), "%s/%s", CHATS_DIR, filename);
    json_error_t err;
    json_t *root = json_load_file(path, 0, &err);
    if (!root) return false;
    memset(chat, 0, sizeof(Chat));
    strncpy(chat->filename, filename, sizeof(chat->filename) - 1);
    json_t *title = json_object_get(root, "title");
    if (json_is_string(title))
        strncpy(chat->title, json_string_value(title), sizeof(chat->title) - 1);
    json_t *msgs = json_object_get(root, "messages");
    if (json_is_array(msgs)) {
        size_t i; json_t *m;
        json_array_foreach(msgs, i, m) {
            if (chat->count >= MAX_HISTORY) break;
            json_t *r = json_object_get(m, "role");
            json_t *c = json_object_get(m, "content");
            if (json_is_string(r) && json_is_string(c)) {
                strncpy(chat->messages[chat->count].role,    json_string_value(r), 15);
                strncpy(chat->messages[chat->count].content, json_string_value(c), MAX_RESPONSE - 1);
                chat->count++;
            }
        }
    }
    json_decref(root);
    return true;
}

void scan_chats(AppCtx *app) {
    app->chat_count = 0;
    DIR *d = opendir(CHATS_DIR);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && app->chat_count < MAX_CHATS) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
            strncpy(app->chat_files[app->chat_count], name, 127);
            Chat tmp;
            if (load_chat(&tmp, name))
                strncpy(app->chat_titles[app->chat_count], tmp.title, 63);
            else
                strncpy(app->chat_titles[app->chat_count], name, 63);
            app->chat_count++;
        }
    }
    closedir(d);
}

void new_chat(AppCtx *app, const char *title) {
    memset(&app->current_chat, 0, sizeof(Chat));
    strncpy(app->current_chat.title, title, sizeof(app->current_chat.title) - 1);
    u64 tick = svcGetSystemTick();
    snprintf(app->current_chat.filename, sizeof(app->current_chat.filename),
             "chat_%llu.json", tick);
}

// ─── Groq API ─────────────────────────────────────────────────────────────────

char *build_body(Chat *chat) {
    json_t *root = json_object();
    json_t *msgs = json_array();

    json_t *sys = json_object();
    json_object_set_new(sys, "role", json_string("system"));
    json_object_set_new(sys, "content", json_string(
        "You are 3DSMate, a friendly and concise AI assistant running on a "
        "Nintendo 3DS. Keep responses short (max 4 sentences) and helpful."));
    json_array_append_new(msgs, sys);

    for (int i = 0; i < chat->count; i++) {
        json_t *m = json_object();
        json_object_set_new(m, "role",    json_string(chat->messages[i].role));
        json_object_set_new(m, "content", json_string(chat->messages[i].content));
        json_array_append_new(msgs, m);
    }

    json_object_set_new(root, "model",                json_string(GROQ_MODEL));
    json_object_set_new(root, "messages",             msgs);
    json_object_set_new(root, "temperature",          json_real(1.0));
    json_object_set_new(root, "max_completion_tokens",json_integer(512));
    json_object_set_new(root, "top_p",                json_real(1.0));
    json_object_set_new(root, "stream",               json_false());
    json_object_set_new(root, "stop",                 json_null());

    char *body = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return body;
}

char *parse_answer(const char *json_str) {
    json_error_t err;
    json_t *root = json_loads(json_str, 0, &err);
    if (!root) return NULL;
    json_t *choices = json_object_get(root, "choices");
    if (!json_is_array(choices) || json_array_size(choices) == 0) { json_decref(root); return NULL; }
    json_t *msg = json_object_get(json_array_get(choices, 0), "message");
    json_t *cnt = json_object_get(msg, "content");
    char *r = NULL;
    if (json_is_string(cnt)) r = strdup(json_string_value(cnt));
    json_decref(root);
    return r;
}

char *send_request(const char *api_key, const char *body) {
    httpcContext ctx;
    char url[256], auth[220];
    snprintf(url,  sizeof(url),  "https://%s%s", GROQ_HOST, GROQ_PATH);
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_POST, url, 1))) return NULL;
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "Content-Type",  "application/json");
    httpcAddRequestHeaderField(&ctx, "Authorization", auth);
    httpcAddPostDataRaw(&ctx, (u8 *)body, strlen(body));

    if (R_FAILED(httpcBeginRequest(&ctx))) { httpcCloseContext(&ctx); return NULL; }

    u32 status = 0;
    httpcGetResponseStatusCode(&ctx, &status);
    if (status != 200) { httpcCloseContext(&ctx); return NULL; }

    char *resp = malloc(MAX_RESPONSE + 1);
    u8   *buf  = malloc(MAX_RESPONSE + 1);
    u32   size = 0;
    httpcDownloadData(&ctx, buf, MAX_RESPONSE, &size);
    buf[size] = '\0';
    memcpy(resp, buf, size + 1);
    free(buf);
    httpcCloseContext(&ctx);
    return resp;
}

// ─── Keyboard ─────────────────────────────────────────────────────────────────

bool keyboard(char *out, size_t max, const char *hint, bool multiline) {
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, max - 1);
    swkbdSetHintText(&swkbd, hint);
    if (multiline) swkbdSetFeatures(&swkbd, SWKBD_MULTILINE);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT,  "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK",     true);
    SwkbdButton btn = swkbdInputText(&swkbd, out, max);
    return (btn == SWKBD_BUTTON_CONFIRM || btn == SWKBD_BUTTON_RIGHT);
}

// ─── Screens ──────────────────────────────────────────────────────────────────

void screen_main_menu(AppCtx *app) {
    draw_header("3DSMate – Main Menu");
    bool has_key = strlen(app->api_key) > 5;
    printf(C_CYAN " API Key: " C_RESET "%s\n\n",
           has_key ? C_GREEN "✓ Set" C_RESET : C_RED "✗ Not set" C_RESET);
    printf(C_YELLOW C_BOLD " Menu:\n" C_RESET);
    printf("  " C_GREEN   "A"     C_RESET " → Start new chat\n");
    printf("  " C_CYAN    "B"     C_RESET " → Browse saved chats (%d)\n", app->chat_count);
    printf("  " C_MAGENTA "X"     C_RESET " → %s API key\n", has_key ? "Change" : "Enter");
    printf("  " C_RED     "START" C_RESET " → Quit\n\n");
    draw_status(app);
}

void screen_chat(AppCtx *app) {
    Chat *c = &app->current_chat;
    draw_header(c->title[0] ? c->title : "3DSMate Chat");
    int start = (c->count > 7) ? c->count - 7 : 0;
    for (int i = start; i < c->count; i++) {
        if (strcmp(c->messages[i].role, "user") == 0) {
            printf(C_GREEN C_BOLD " You:\n" C_RESET);
            print_wrapped(c->messages[i].content, C_GREEN, 3);
        } else {
            printf(C_YELLOW C_BOLD " 3DSMate:\n" C_RESET);
            print_wrapped(c->messages[i].content, C_WHITE, 3);
        }
        printf("\n");
    }
    printf(C_CYAN "A" C_RESET "=Send  "
           C_MAGENTA "X" C_RESET "=Clear  "
           C_RED "B" C_RESET "=Back\n");
    draw_status(app);
}

void screen_chat_list(AppCtx *app) {
    draw_header("3DSMate – Saved Chats");
    if (app->chat_count == 0) {
        printf(C_YELLOW " No saved chats yet.\n" C_RESET);
        printf(" Press " C_RED "B" C_RESET " to go back.\n");
        return;
    }
    printf(C_YELLOW C_BOLD " Your Chats:\n" C_RESET);
    for (int i = 0; i < app->chat_count; i++) {
        if (i == app->selected_chat)
            printf(C_CYAN C_BOLD " ▶ %d. %s\n" C_RESET, i + 1, app->chat_titles[i]);
        else
            printf("   %d. %s\n", i + 1, app->chat_titles[i]);
    }
    printf("\n" C_GREEN "A" C_RESET "=Open  "
           C_RED "Y" C_RESET "=Delete  "
           C_CYAN "↑↓" C_RESET "=Navigate  "
           C_RED "B" C_RESET "=Back\n");
    draw_status(app);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    httpcInit(0);
    ensure_dirs();

    AppCtx app;
    memset(&app, 0, sizeof(AppCtx));
    app.state = STATE_MAIN_MENU;

    load_api_key(app.api_key, sizeof(app.api_key));
    scan_chats(&app);

    if (strlen(app.api_key) > 5)
        set_status(&app, "API key loaded!", true);
    else
        set_status(&app, "Please set your Groq API key (press X).", false);

    screen_main_menu(&app);

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        u32 keys = hidKeysDown();

        if (keys & KEY_START) break;

        // ── Main Menu ─────────────────────────────────────────────────────────
        if (app.state == STATE_MAIN_MENU) {
            if (keys & KEY_A) {
                if (strlen(app.api_key) < 5) {
                    set_status(&app, "Please set your API key first (X)!", false);
                    screen_main_menu(&app);
                } else {
                    char title[64] = {0};
                    if (keyboard(title, sizeof(title), "Enter chat title...", false)) {
                        if (strlen(title) == 0) strncpy(title, "New Chat", 63);
                        new_chat(&app, title);
                        app.state = STATE_CHAT;
                        set_status(&app, "", true);
                        screen_chat(&app);
                    } else {
                        screen_main_menu(&app);
                    }
                }
            }
            if (keys & KEY_B) {
                scan_chats(&app);
                app.selected_chat = 0;
                app.state = STATE_CHAT_LIST;
                set_status(&app, "", true);
                screen_chat_list(&app);
            }
            if (keys & KEY_X) {
                char key[200] = {0};
                if (keyboard(key, sizeof(key), "Enter your Groq API key...", false)) {
                    if (strlen(key) > 5) {
                        strncpy(app.api_key, key, sizeof(app.api_key) - 1);
                        save_api_key(app.api_key);
                        set_status(&app, "API key saved!", true);
                    } else {
                        set_status(&app, "API key too short!", false);
                    }
                }
                screen_main_menu(&app);
            }
        }

        // ── Chat ──────────────────────────────────────────────────────────────
        else if (app.state == STATE_CHAT) {
            if (keys & KEY_B) {
                save_chat(&app.current_chat);
                scan_chats(&app);
                app.state = STATE_MAIN_MENU;
                set_status(&app, "Chat saved.", true);
                screen_main_menu(&app);
            }
            if (keys & KEY_X) {
                Chat *c = &app.current_chat;
                char title_bak[64], file_bak[128];
                strncpy(title_bak, c->title,    sizeof(title_bak) - 1);
                strncpy(file_bak,  c->filename, sizeof(file_bak)  - 1);
                memset(c, 0, sizeof(Chat));
                strncpy(c->title,    title_bak, sizeof(c->title)    - 1);
                strncpy(c->filename, file_bak,  sizeof(c->filename) - 1);
                save_chat(c);
                set_status(&app, "Chat history cleared.", true);
                screen_chat(&app);
            }
            if (keys & KEY_A) {
                char input[MAX_INPUT] = {0};
                if (keyboard(input, sizeof(input), "Message 3DSMate...", true)) {
                    if (strlen(input) == 0) goto skip;
                    Chat *c = &app.current_chat;
                    if (c->count < MAX_HISTORY) {
                        strncpy(c->messages[c->count].role,    "user", 15);
                        strncpy(c->messages[c->count].content, input, MAX_RESPONSE - 1);
                        c->count++;
                    }
                    screen_chat(&app);
                    printf(C_YELLOW "\n ⏳ 3DSMate is thinking..." C_RESET "\n");
                    gfxFlushBuffers(); gfxSwapBuffers();

                    char *body = build_body(c);
                    char *raw  = send_request(app.api_key, body);
                    free(body);

                    const char *reply;
                    if (raw) {
                        char *ans = parse_answer(raw);
                        free(raw);
                        reply = ans ? ans : "[Parse error]";
                    } else {
                        reply = "[Network error – check WiFi!]";
                    }

                    if (c->count < MAX_HISTORY) {
                        strncpy(c->messages[c->count].role,    "assistant", 15);
                        strncpy(c->messages[c->count].content, reply, MAX_RESPONSE - 1);
                        c->count++;
                    }
                    save_chat(c);
                    set_status(&app, "Saved.", true);
                    screen_chat(&app);
                }
                skip:;
            }
        }

        // ── Chat List ─────────────────────────────────────────────────────────
        else if (app.state == STATE_CHAT_LIST) {
            if (keys & KEY_B) { app.state = STATE_MAIN_MENU; screen_main_menu(&app); }
            if ((keys & KEY_UP)   && app.selected_chat > 0)                   { app.selected_chat--; screen_chat_list(&app); }
            if ((keys & KEY_DOWN) && app.selected_chat < app.chat_count - 1)  { app.selected_chat++; screen_chat_list(&app); }
            if ((keys & KEY_A) && app.chat_count > 0) {
                if (load_chat(&app.current_chat, app.chat_files[app.selected_chat])) {
                    app.state = STATE_CHAT;
                    set_status(&app, "Chat loaded!", true);
                    screen_chat(&app);
                } else {
                    set_status(&app, "Failed to load chat!", false);
                    screen_chat_list(&app);
                }
            }
            if ((keys & KEY_Y) && app.chat_count > 0) {
                char path[200];
                snprintf(path, sizeof(path), "%s/%s", CHATS_DIR, app.chat_files[app.selected_chat]);
                remove(path);
                scan_chats(&app);
                if (app.selected_chat >= app.chat_count && app.selected_chat > 0) app.selected_chat--;
                set_status(&app, "Chat deleted.", true);
                screen_chat_list(&app);
            }
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    httpcExit();
    gfxExit();
    return 0;
}
