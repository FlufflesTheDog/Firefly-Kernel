#pragma once
#include <algorithm.h>
#include <cstdlib/cstdint.h>
#include <cstdlib/cstring.h>

enum class color : uint8_t {
    black = 0x00,
    blue = 0x01,
    green = 0x02,
    cyan = 0x03,
    red = 0x04,
    magenta = 0x05,
    brown = 0x06,
    lightgrey = 0x07,
    darkgrey = 0x08,
    lightblue = 0x09,
    lightgreen = 0x0A,
    lightcyan = 0x0B,
    lightred = 0x0C,
    lightmagenta = 0x0D,
    lightbrown = 0x0E,
    white = 0x0F,
};

struct __attribute__((packed)) vga_char {
    char codepoint;
    color fg : 4;
    color bg : 4;
};

static_assert(2 == sizeof(vga_char), "vga_char size incorrect");

constexpr static size_t width = 80;
constexpr static size_t height = 15;
constexpr static size_t display_buff_addr = 0xB8000;

struct Display {

    // cursor corrdinates
    // should probably be in a separate cursor struct and a static std_cursor
    // so one `write` passes the std_cursor to another write which takes it or any other created by the user
    // similar to printf's only job being to forward the arguments and `stdout` to vfprintf
    size_t y = 0;
    size_t x = 0;

    // default colors
    // also should go in a `cursor` struct with x and y
    color background = color::black;
    color foreground = color::white;

    vga_char* display_buffer = reinterpret_cast<vga_char*>(display_buff_addr);

    // char16_t prepare_char(const char16_t c) const noexcept {
    //     return c | (((background << 4) | (foreground & 0x0F)) << 8);
    // }

    void clear() {
        // fails, no matching function call
        // firefly::std::fill(
        //     display_buffer, display_buffer + (height * width), {' ', color::black, color::white}
        // );
    }

    static size_t vgalen(const vga_char* str) {
        size_t _sz{};
        while (str[_sz++].codepoint);
        return _sz;
    }

    void handle_write_pos() {
        if (x >= width) {
            y++;
            x = 0;
        }
        if (y >= height) {
            firefly::std::copy(display_buffer + width, display_buffer + ((height - 1) * width), display_buffer);
            y = height - 1;
        }
    }

    bool handle_special_characters(const char c) {
        bool ret = false;
        if (c == '\n') {
            x = 0;
            y++;
            ret = true;
        }
        if (c == '\t') {
            x += 4;
            ret = true;
        }
        if (c == '\r') {
            x = 0;
            ret = true;
        }
        if (ret) handle_write_pos();
        return ret;
    }

    void write(const vga_char c) {
        if (handle_special_characters(c.codepoint)) {
            return;
        }
        display_buffer[y * width + x++] = c;
        handle_write_pos();
    }

    void write(const vga_char* str) {
        for (size_t idx = 0; idx < vgalen(str); idx++) {
            write(str[idx]);
        }
    }

    void write(const char c) {
        if (handle_special_characters(c)) {
            return;
        }
        display_buffer[y * width + x++] = {c, foreground, background};
        handle_write_pos();
    }

    void write(const char* str) {
        for (size_t idx = 0; idx < strlen(str); idx++) {
            write(str[idx]);
        }
    }

    void set_background_color(color c) {
        background = c;
    }

    void set_foreground_color(color c) {
        foreground = c;
    }
};