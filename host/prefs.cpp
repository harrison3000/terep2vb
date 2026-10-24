// The chooser's preferences file (prefs.hpp).
#include <cstdio>
#include <cstdlib>
#include <SDL.h>
#include "host.hpp"
#include "prefs.hpp"

static std::string saved;              // the file's contents as last read or written

static std::string prefs_path()
{
    char *dir = SDL_GetPrefPath(nullptr, "terep2vb");
    if (!dir)
        return "";
    std::string path = std::string(dir) + "prefs.txt";
    SDL_free(dir);
    return path;
}

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static const char *on_off(bool b) { return b ? "on" : "off"; }

// The preferences as the file's text.
static std::string prefs_text(const HostOptions &host, const std::string &map)
{
    std::string t;
    t += "scale: " + std::to_string(render_scale) + "\n";
    t += std::string("minimap: ") + on_off(host.minimap) + "\n";
    t += std::string("sound: ") + on_off(!host.no_sound) + "\n";
    for (int i = 0; i < extra_option_count; i++)
        t += std::string(extra_option_list[i].name) + ": " + on_off(extra_options.*extra_option_list[i].flag) + "\n";
    if (!map.empty())
        t += "map: " + map + "\n";
    return t;
}

void prefs_load(HostOptions *host, std::string *map)
{
    std::string path = prefs_path();
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        size_t colon = l.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = trim(l.substr(0, colon)), value = trim(l.substr(colon + 1));
        bool on = value == "on";
        if (key == "scale") {
            int s = atoi(value.c_str());
            if (s >= 1 && s <= MAX_RENDER_SCALE)
                render_scale = s;
        } else if (key == "minimap") {
            host->minimap = on;
        } else if (key == "sound") {
            host->no_sound = !on;
        } else if (key == "map") {
            *map = value;
        } else {
            for (int i = 0; i < extra_option_count; i++)
                if (key == extra_option_list[i].name)
                    extra_options.*extra_option_list[i].flag = on;
        }
    }
    fclose(f);
    saved = prefs_text(*host, *map);
}

void prefs_save(const HostOptions &host, const std::string &map)
{
    std::string text = prefs_text(host, map);
    if (text == saved)
        return;
    std::string path = prefs_path();
    FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "could not write the preferences (%s)\n", path.c_str());
        return;
    }
    fputs(text.c_str(), f);
    fclose(f);
    saved = text;
}
