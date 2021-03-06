#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/errno.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <memory>
#include <stdexcept>

static char const* sc_clear = "\33[H\33[2J"; // FIXME should come from terminfo
static int quit = 0;

static int usage() {
    fprintf(stderr, "usage: banner [-c color] [-d delay-ms] [-f font-file] [-F fill-char] [-h screen-height] [-i incr-chars] [-w screen-width] message\n");
    throw std::runtime_error("usage");
}

// -----------------------------------------------------------------
class CharRect {
public:
    explicit CharRect(int width, int height, char fill = ' ', int kern = 0)
        : width_(width), height_(height), bytes_(size()), fill_(fill), kern_(kern) {
        clear();
    }
    explicit CharRect(const CharRect& cr)
        : width_(cr.width()), height_(cr.height()), fill_(cr.fill()), kern_(cr.kern()) {
        bytes_.resize(size());
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
    void clear() {
        clear(fill_);
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
        bytes_.resize(size());
        clear();
        blit(&old_cr, 0, 0, 0, 0);
    }
protected:
    int index(int col, int row) const {
        return row*width_ + col;
    }
    int size() const {
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
    // font_file: (hdr_line char_line+)* fin_line
    // hdr_line:  '=' CHAR (CHAR '=' NUMBER)* '\n'
    // char_line: ' ' CHAR* '$'? '\n'
    // fin_line:  '=' '=' '\n'
    //   Initial ' ' and final '$' in char_line are ignored.
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
        char curr_kern = 0;
        char line[1024];
        int linenum = 0;
        while (fgets(line, sizeof(line), fd) != NULL) {
            ++linenum;
            char * const nl = strchr(line, '\n');
            if (nl != NULL) *nl = '\0';
            int kern = 0;
            char headch;
            if ((headch = hdr_line(line, filename, linenum, &kern)) != '\0') {
                if (rows.size() > 0) {
                    CharRect* cr = new CharRect (max_len, rows.size(), fill, curr_kern);
                    cr->init(rows);
                    set_char_image(curr_ch, cr);
                    rows.clear();
                }
                curr_ch = headch;
                curr_kern = kern;
                max_len = 0;
            } else if (line[0] == ' ') {
                std::string row = std::string(&line[1]);
                int len = (int) row.size();
                if (row[len-1] == '$')
                    row.pop_back();
                if (len > max_len) max_len = len;
                rows.push_back(row);
            } else {
                fprintf(stderr, "%s:%d: invalid line\n", filename.c_str(), linenum);
                return false;
            }
        }
        fclose(fd);
        return true;
    }
    // header line is "=CHAR" followed by zero or more "KEY=NUMBER"
    char hdr_line(char const* line, std::string const& filename, int linenum, int* kern) {
        char const* p = line;
        if (*p++ != '=')
            return '\0';
        char headch = *p++;
        if (headch == '\0') {
            fprintf(stderr, "%s:%d: lone '=' line\n", filename.c_str(), linenum);
            return '\0';
        }
        for (;;) {
            while (*p == ' ')
                ++p;
            if (*p == '\0')
                break;
            char key = *p++;
            if (*p++ != '=') {
                fprintf(stderr, "%s:%d: incomplete %c key\n", filename.c_str(), linenum, key);
                return '\0';
            }
            char *ep;
            int num = (int) strtol(p, &ep, 10);
            p = ep;
            switch (key) {
            case 'k': *kern = num; break;
            default:
                fprintf(stderr, "%s:%d: unknown %c key\n", filename.c_str(), linenum, key);
                return '\0';
            }
        }
        return headch;
    }
private:
    std::map<char, const CharRect* > lib_;
};

// -----------------------------------------------------------------
class Banner {
public:
    Banner(std::string const& message, Font const& font) : img_(0,0) {
        // Build banner image from char images.
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
                int ocol = col + offset;
                (*putc)((ocol < 0) ? ' ' : img_.get_at(ocol, row));
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
    Params(int argc, char* const argv[]) {
        sc_width = atoi(getenv("COLUMNS"));
        sc_height = atoi(getenv("LINES"));
        delay_ms = 50;
        offset_incr = 1;
        fill = ' ';
        color = "";
        font_file = "";
        int ch;
        while ((ch = getopt(argc, argv, "c:d:f:F:h:i:w:x:y:")) != -1) {
            switch (ch) {
            case 'c': color = optarg; break;
            case 'd': delay_ms = atoi(optarg); break;
            case 'f': font_file = optarg; break;
            case 'F': fill = optarg[0]; break;
            case 'h': sc_height = atoi(optarg); break;
            case 'i': offset_incr = atoi(optarg); break;
            case 'w': sc_width = atoi(optarg); break;
            default: usage();
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
    char fill;
    std::string color;
    std::string font_file;
    std::string message;
};

// -----------------------------------------------------------------
class Runner {
public:
    Runner(Params const& params) : params_(params) {}
    void run() {
        rawmode(true);
        Font font (params_.font_file);
        Banner banner(params_.message, font);
        put_color(params_.color);
        for (int offset = -params_.sc_width; !quit; offset += params_.offset_incr) {
            if (key_pressed() == 'q') break;
            banner.print(offset, params_.sc_width, params_.sc_height, putch);
            sleep_ms(params_.delay_ms);
        }
        put_color("");
        printf("%s", sc_clear);
        rawmode(false);
    }
    void put_color(std::string const& color) {
        if (color.size() == 0) {
            printf("\33[m");
        } else {
            printf("\33[%dm", parse_color(color[0])); // fg
            if (color.size() >= 1)
                printf("\33[%dm", parse_color(color[1])+10); // bg
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
    static void sleep_ms(int ms) {
        struct timespec tv;
        tv.tv_sec = ms / 1000;
        tv.tv_nsec = (ms % 1000) * 1000000;
        nanosleep(&tv, NULL);
    }
    static void rawmode(bool raw) {
        struct termios term;
        static struct termios save_term;
        if (raw) {
            if (tcgetattr(0, &term) < 0)
                throw std::runtime_error("cannot get tty attributes");
			save_term = term;
            term.c_lflag &= ~ICANON;
		} else {
            term = save_term;
        }
        tcsetattr(0, TCSADRAIN, &term);
    }
    static char key_pressed() {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        struct timeval tv;
        tv.tv_sec = tv.tv_usec = 0;
        int r = select(1, &fds, NULL, NULL, &tv);
        if (r > 0) return getchar();
        return '\0';
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

int main(int argc, char* const argv[]) {
    signal(SIGINT, intr);
    try {
        Params params (argc, argv);
        Runner runner(params);
        runner.run();
    } catch (std::runtime_error& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}
