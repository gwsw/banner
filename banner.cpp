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
    CharRect(int width, int height, char fill = ' ', int kern = 0)
        : width_(width), height_(height), bytes_(width*height), kern_(kern) {
        clear(fill);
    }
    int width() const { return width_; }
    int height() const { return height_; }
    int kern() const { return kern_; }
    char get_at(int row, int col) const {
        if (row >= width_ || col >= height_) return ' ';
        return bytes_[row*width_ + col];
    }
    void set_at(int row, int col, char ch) {
        if (row >= width_ || col >= height_) return;
        bytes_[row*width_ + col] = ch;
    }
    void clear(char ch) {
        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                set_at(row, col, ch);
            }
        }
    }
    void blit(std::shared_ptr<const CharRect> const from, int frow, int fcol, int trow, int tcol, int bw = -1, int bh = -1) {
        if (bw < 0) bw = from->width();
        if (bh < 0) bh = from->height();
        for (int row = 0; row < bh; ++row) {
            for (int col = 0; col < bw; ++col) {
                char ch = from->get_at(frow+row, fcol+col);
                if (ch != ' ')
                    set_at(trow+row, tcol+col, ch);
            }
        }
    }
    void init(std::list<std::string>& rows) {
        int row = 0;
        for (auto it = rows.begin(); it != rows.end(); ++it, ++row) {
            std::string& str = *it;
            for (int col = 0; col < (int) it->size(); ++col) {
                set_at(row, col, str[col]);
            }
        }
    }
    void resize(int width, int height) {
        int len = width * height;
        if (len < width_ * height_) return;
        bytes_.resize(len);
        width_ = width;
        height_ = height;
        clear(' ');
    }
private:
    int width_;
    int height_;
    std::vector<char> bytes_;
    int kern_;
};

// -----------------------------------------------------------------
class Font {
public:
    Font(std::string const& filename) {
        if (!parse(filename))
            throw std::exception();
    }
    std::shared_ptr<const CharRect> char_image(char ch) const {
        return lib_[ch];
    }
protected:
    void set_char_image(char ch, std::shared_ptr<const CharRect> img) {
        lib_[ch] = img;
    }
    bool parse(std::string const& filename) {
        FILE* fd = fopen(filename.c_str(), "r");
        if (fd == NULL) return false;
        std::list<std::string> rows;
        int max_len = 0;
        char curr_ch = '\0';
        char line[1024];
        while (fgets(line, sizeof(line), fd) != NULL) {
            char * const nl = strchr(line, '\n');
            if (nl != NULL) *nl = '\0';
            if (line[0] == '=' && line[1] != '\0' && line[2] == '\0') {
                if (rows.size() > 0) {
					std::shared_ptr<CharRect> cr (new CharRect(rows.size(), max_len));
                    cr->init(rows);
                    set_char_image(curr_ch, cr);
                    rows.clear();
                }
                max_len = 0;
                curr_ch = line[1];
            } else {
                int len = strlen(line);
                if (len > max_len) max_len = len;
                rows.push_back(std::string(line));
            }
        }
        fclose(fd);
        return true;
    }
private:
    std::map<char,std::shared_ptr<const CharRect> > lib_;
};

// -----------------------------------------------------------------
class Banner {
public:
    Banner(std::string const& str, Font const& font) : img_(0,0) {
        build(str, font);
    }
    void print(int offset, int sc_width, int sc_height) const {
        for (int row = 0; row < sc_height; ++row) {
            for (int col = 0; col < sc_width; ++col) {
                ::putchar(img_.get_at(row, col+offset));
            }
            ::putchar('\n');
        }
    }
protected:
    void build(std::string const& str, Font const& font) {
        for (size_t i = 0; i < str.size(); ++i) {
            char ch = str[i];
            std::shared_ptr<const CharRect> ch_img = font.char_image(ch);
            int w = img_.width();
            img_.resize(img_.width() + ch_img->width(), std::max(img_.height(), ch_img->height()));
            img_.blit(ch_img, 0, 0, w, 0);
        }
    }
private:
    CharRect img_;
};

// -----------------------------------------------------------------

static void run_banner(std::string const& str, std::string const& font_file, int delay_ms, int offset_incr, int sc_width, int sc_height, int space) {
    struct timespec delay_tv;
    delay_tv.tv_sec = 0;
    delay_tv.tv_nsec = delay_ms * 1000000;
    Font font (font_file);
    Banner banner(str, font);
    ////std::string space_str; for (int i = 0; i < space; ++i) space_str += " ";
    for (int offset = 0;; offset += offset_incr) {
        banner.print(offset, sc_width, sc_height);
        nanosleep(&delay_tv, NULL);
    }
}

int main(int argc, char** argv) {
    int sc_width = atoi(getenv("COLUMNS"));
    int sc_height = atoi(getenv("LINES"));
    int delay = 100;
    int offset_incr = 1;
    int space = 3;
    std::string font_file = "?";
    int ch;
    while ((ch = getopt(argc, argv, "d:f:h:i:s:w:")) != -1) {
        switch (ch) {
        case 'd':
            delay = atoi(optarg);
            break;
        case 'f':
            font_file = optarg;
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
    run_banner(str, font_file, delay, offset_incr, sc_width, sc_height, space);
    return 0;
}
