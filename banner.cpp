#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <memory>

static int usage() {
    printf("usage: \n");
    return 1;
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
        ///if (width == width_ && height == height_) return;
        CharRect old_cr (*this);
        width_ = width;
        height_ = height;
        bytes_.resize(len());
        clear(fill_);
        blit(&old_cr, 0, 0, 0, 0, -1, -1, true);
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
        if (!parse(filename, fill))
            throw std::exception();
    }
    const CharRect* char_image(char ch) const {
        return lib_.at(ch);
    }
protected:
    void set_char_image(char ch, const CharRect* img) {
        lib_[ch] = img;
    }
    bool parse(std::string const& filename, char fill) {
        FILE* fd = fopen(filename.c_str(), "r");
        if (fd == NULL) {
            fprintf(stderr, "cannot open %s: errno %d\n", filename.c_str(), errno);
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
    Banner(std::string const& str, Font const& font) : img_(0,0) {
        build(str, font);
    }
    void print(int offset, int sc_width, int sc_height, void (*putc)(char ch)) const {
        for (char const* p = "\33[H\33[J"; *p; ++p)
            (*putc)(*p);
        int rows = std::min(img_.height(), sc_height-1);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < sc_width; ++col) {
                (*putc)(img_.get_at(col+offset, row));
            }
            (*putc)('\n');
        }
    }
protected:
    void build(std::string const& str, Font const& font) {
        for (size_t i = 0; i < str.size(); ++i) {
            char ch = str[i];
            CharRect const* ch_img = font.char_image(ch);
            int redge = img_.width();
            img_.resize(img_.width() + ch_img->width(), std::max(img_.height(), ch_img->height()));
            img_.blit(ch_img, 0, 0, redge, 0);
        }
    }
private:
    CharRect img_;
};

// -----------------------------------------------------------------

static void putch(char ch) {
    putchar(ch);
}

static void run_banner(std::string const& str, std::string const& font_file, int delay_ms, int offset_incr, int sc_width, int sc_height, int space, char fill) {
    struct timespec delay_time;
    if (delay_ms >= 0) {
        delay_time.tv_sec = delay_ms / 1000;
        delay_time.tv_nsec = (delay_ms % 1000) * 1000000;
    }
    Font font (font_file);
    Banner banner(str, font);
    for (int offset = 0;; offset += offset_incr) {
        banner.print(offset, sc_width, sc_height, putch);
        if (delay_ms < 0) break;
        nanosleep(&delay_time, NULL);
    }
}

int main(int argc, char** argv) {
    int sc_width = atoi(getenv("COLUMNS"));
    int sc_height = atoi(getenv("LINES"));
    int delay_ms = -1;
    int offset_incr = 1;
    int space = 3;
    int fill = ' ';
    std::string font_file = "?";
    int ch;
    while ((ch = getopt(argc, argv, "d:f:F:h:i:s:w:")) != -1) {
        switch (ch) {
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
        case 's':
            space = atoi(optarg);
            break;
        case 'w':
            sc_width = atoi(optarg);
            break;
        default:
            return usage();
        }
    }
    std::string str = "";
    for (; optind < argc; ++optind) {
        if (str != "") str += " ";
        str += argv[optind];
    }
    run_banner(str, font_file, delay_ms, offset_incr, sc_width, sc_height, space, fill);
    return 0;
}
