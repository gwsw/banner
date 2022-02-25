#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/errno.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <memory>
#include <stdexcept>

static char const* sc_clear = "\33[H\33[2J";
static int quit = 0;

static int usage() {
    fprintf(stderr, "usage: banner [-c color] [-d delay-ms] [-f font-file] [-F fill-char] [-h screen-height] [-i incr-chars] [-w screen-width] message\n");
    throw std::runtime_error("usage");
}

// -----------------------------------------------------------------
class CharRect {
public:
    explicit CharRect(int width, int height, char fill = ' ', int kern = 0)
        : width_(width), height_(height), bytes_(len()), fill_(fill), kern_(kern) {
        clear(fill);
    }
    explicit CharRect(const CharRect& cr)
        : width_(cr.width()), height_(cr.height()), fill_(cr.fill()), kern_(cr.kern()) {
        bytes_.resize(len());
        bytes_ = cr.bytes_;
    }
    int width() const { return width_; }
    int height() const { return height_; }
    char fill() const { return fill_; }
    int kern() const { return kern_; }
    char get_at(int col, int row) const {
        if (row >= height_ || width_ == 0) return fill_;
        return bytes_[index(col % width_, row)];
    }
    void set_at(int col, int row, char ch) {
        if (row >= height_ || col >= width_) return;
        bytes_[index(col,row)] = ch;
    }
    void clear(char ch) {
        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                set_at(col, row, ch);
            }
        }
    }
    void blit(const CharRect* const from, int fcol, int frow, int tcol, int trow, int bw = -1, int bh = -1, bool alpha = false) {
        if (bw < 0) bw = from->width();
        if (bh < 0) bh = from->height();
        for (int row = 0; row < bh; ++row) {
            for (int col = 0; col < bw; ++col) {
                char ch = from->get_at(fcol+col, frow+row);
                if (!alpha || ch != fill_)
                    set_at(tcol+col, trow+row, ch);
            }
        }
    }
    void init(std::list<std::string>& rows) {
        auto it = rows.begin(); 
        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                set_at(col, row, (it == rows.end() || col >= (int) it->size()) ? fill_ : it->at(col));
            }
            if (it != rows.end()) ++it;
        }
    }
    void resize(int width, int height) {
        if (width < width_) width = width_;
        if (height < height_) height = height_;
        CharRect old_cr (*this);
        width_ = width;
        height_ = height;
        bytes_.resize(len());
        clear(fill_);
        blit(&old_cr, 0, 0, 0, 0);
    }
protected:
    int index(int col, int row) const {
        return row*width_ + col;
    }
    int len() const {
        return width_ * height_;
    }
private:
    int width_;
    int height_;
    std::vector<char> bytes_;
    char fill_;
    int kern_;
};

// -----------------------------------------------------------------
class Font {
public:
    Font(std::string const& filename, char fill = ' ') {
        if (!parse_font_file(filename, fill))
            throw std::runtime_error("cannot parse font file");
    }
    const CharRect* char_image(char ch) const {
        try {
            return lib_.at(ch);
        } catch (...) {
            ///return lib_.at('?');
            fprintf(stderr, "'%c' not in font\n", ch);
            throw;
        }
    }
protected:
    void set_char_image(char ch, const CharRect* img) {
        lib_[ch] = img;
    }
    bool parse_font_file(std::string const& filename, char fill) {
        if (filename.size() == 0) {
            fprintf(stderr, "no font file specified\n");
            return false;
        }
        FILE* fd = fopen(filename.c_str(), "r");
        if (fd == NULL) {
            fprintf(stderr, "cannot open font file %s: %s\n", filename.c_str(), strerror(errno));
            return false;
        }
        std::list<std::string> rows;
        int max_len = 0;
        char curr_ch = '\0';
        char line[1024];
        int linenum = 0;
        while (fgets(line, sizeof(line), fd) != NULL) {
            ++linenum;
            char * const nl = strchr(line, '\n');
            if (nl != NULL) *nl = '\0';
            int kern = 0;
            if (hdr_line(line, &kern)) {
                if (rows.size() > 0) {
                    CharRect* cr = new CharRect (max_len, rows.size(), fill, kern);
                    cr->init(rows);
                    set_char_image(curr_ch, cr);
                    rows.clear();
                }
                max_len = 0;
                curr_ch = line[1];
            } else if (line[0] == ' ') {
                for (char* p = line; *p != '\0'; ++p)
                    if (*p == '_') *p = ' ';
                std::string row = std::string(&line[1]);
                if ((int)row.size() > max_len) max_len = row.size();
                rows.push_back(row);
            } else {
                fprintf(stderr, "invalid line [%d] in %s: %s\n", linenum, filename.c_str(), line);
                return false;
            }
        }
        fclose(fd);
        return true;
    }
    bool hdr_line(char const* line, int* kern) {
        if (line[0] != '=' || line[1] == '\0') return false;
        for (char const* p = &line[2]; ; ) { while (*p == ' ')
                ++p;
            if (*p == '\0')
                break;
            if (p[1] != '=') {
                ++p;
            } else {
                char *ep;
                int num = (int) strtol(&p[2], &ep, 10);
                switch (p[0]) {
                case 'k': *kern = num; break;
                }
                p = ep;
            }
        }
        return true;
    }
private:
    std::map<char, const CharRect* > lib_;
};

// -----------------------------------------------------------------
class Banner {
public:
    Banner(std::string const& message, Font const& font) : img_(0,0) {
        for (size_t i = 0; i < message.size(); ++i) {
            char ch = message[i];
            CharRect const* ch_img = font.char_image(ch);
            int redge = img_.width();
            img_.resize(redge + ch_img->width(), std::max(img_.height(), ch_img->height()));
            img_.blit(ch_img, 0, 0, redge, 0);
        }
    }
    void print(int offset, int sc_width, int sc_height, void (*putc)(char ch)) const {
        for (char const* p = sc_clear; *p; ++p)
            (*putc)(*p);
        int rows = std::min(img_.height(), sc_height-1);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < sc_width; ++col) {
                    (*putc)(img_.get_at(col+offset, row));
            }
            (*putc)('\n');
        }
    }
private:
    CharRect img_;
};

// -----------------------------------------------------------------
class Params {
public:
    Params(int argc, char** argv) {
        sc_width = atoi(getenv("COLUMNS"));
        sc_height = atoi(getenv("LINES"));
        delay_ms = -1;
        offset_incr = 1;
        fill = ' ';
        color = "";
        font_file = "";
        int ch;
        while ((ch = getopt(argc, argv, "c:d:f:F:h:i:w:x:y:")) != -1) {
            switch (ch) {
            case 'c':
                color = optarg;
                break;
            case 'd':
                delay_ms = atoi(optarg);
                break;
            case 'f':
                font_file = optarg;
                break;
            case 'F':
                fill = optarg[0];
                break;
            case 'h':
                sc_height = atoi(optarg);
                break;
            case 'i':
                offset_incr = atoi(optarg);
                break;
            case 'w':
                sc_width = atoi(optarg);
                break;
            default:
                usage();
            }
        }
        if (optind == argc)
            usage();
        for (; optind < argc; ++optind) {
            if (message != "") message += " ";
            message += argv[optind];
        }
    }
public:
    int sc_width;
    int sc_height;
    int delay_ms;
    int offset_incr;
    int fill;
    std::string color;
    std::string font_file;
    std::string message;
};

// -----------------------------------------------------------------
class Runner {
public:
    Runner(Params const& params) : params_(params) {}
    void put_color(std::string const& color) {
        if (color.size() == 0) {
            printf("\33[m");
        } else {
            int color_fg = parse_color(color[0]);
            int color_bg = (color.size() <= 1) ? -1 : parse_color(color[1]);
            if (color_fg > 0)
                printf("\33[%dm", color_fg);
            if (color_bg > 0)
                printf("\33[%dm", color_bg+10);
        }
    }
	int parse_color(char ch) {
        switch (ch) {
        case 'k': return 30;
        case 'r': return 31;
        case 'g': return 32;
        case 'y': return 33;
        case 'b': return 34;
        case 'm': return 35;
        case 'c': return 36;
        case 'w': return 37;
        case 'K': return 90;
        case 'R': return 91;
        case 'G': return 92;
        case 'Y': return 93;
        case 'B': return 94;
        case 'M': return 95;
        case 'C': return 96;
        case 'W': return 97;
        default:  return 0;
        }
    }
    void run() {
        struct timespec delay_time;
        if (params_.delay_ms >= 0) {
            delay_time.tv_sec = params_.delay_ms / 1000;
            delay_time.tv_nsec = (params_.delay_ms % 1000) * 1000000;
        }
        Font font (params_.font_file);
        Banner banner(params_.message, font);
        put_color(params_.color);
        for (int offset = 0; !quit; offset += params_.offset_incr) {
            banner.print(offset, params_.sc_width, params_.sc_height, putch);
            if (params_.delay_ms < 0) break;
            nanosleep(&delay_time, NULL);
        }
        put_color("");
        if (params_.delay_ms >= 0)
            printf("%s", sc_clear);
    }
    static void putch(char ch) {
        putchar(ch);
    }
private:
    Params const& params_;
};

// -----------------------------------------------------------------

static void intr(int sig) {
    quit = 1;
}

int main(int argc, char** argv) {
    Params params (argc, argv);
    signal(SIGINT, intr);
    Runner runner(params);
    runner.run();
    return 0;
}
