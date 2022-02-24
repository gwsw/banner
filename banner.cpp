#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <map>
#include <list>
///#include <vector>
#include <memory>

static int usage() {
    printf("usage: \n");
    return 1;
}
int mjn= 1;

// -----------------------------------------------------------------
class CharRect {
public:
    explicit CharRect(int width, int height, char fill = ' ', int kern = 0)
        : width_(width), height_(height), fill_(fill), kern_(kern) {
		bytes_ = new char[len()];
        clear(fill);
    }
	explicit CharRect(const CharRect& cr)
		: width_(cr.width()), height_(cr.height()), fill_(cr.fill()), kern_(cr.kern()) {
		bytes_ = new char[len()];
		memcpy(bytes_, cr.bytes_, len());
	}
    int width() const { return width_; }
    int height() const { return height_; }
    char fill() const { return fill_; }
    int kern() const { return kern_; }
    char get_at(int row, int col) const {
        if (row >= height_ || width_ == 0) return fill_;
        return bytes_[index(row, col % width_)];
    }
    void set_at(int row, int col, char ch) {
        if (row >= height_ || col >= width_) return;
        bytes_[index(row,col)] = ch;
    }
    void clear(char ch) {
printf("clear('%c') %d x %d\n", ch, width_, height_);
        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                set_at(row, col, ch);
            }
        }
    }
    void blit(const CharRect* const from, int frow, int fcol, int trow, int tcol, int bw = -1, int bh = -1, bool transparent = false) {
        if (bw < 0) bw = from->width();
        if (bh < 0) bh = from->height();
        for (int row = 0; row < bh; ++row) {
            for (int col = 0; col < bw; ++col) {
                char ch = from->get_at(frow+row, fcol+col);
                if (!transparent || ch != fill_)
                    set_at(trow+row, tcol+col, ch);
            }
        }
    }
    void init(std::list<std::string>& rows) {
        auto it = rows.begin(); 
        for (int row = 0; row < height_; ++row) {
            for (int col = 0; col < width_; ++col) {
                set_at(row, col, (it == rows.end() || col >= (int) it->size()) ? fill_ : it->at(col));
				if (it != rows.end()) ++it;
            }
        }
    }
    void resize(int width, int height) {
printf("resize(%d,%d) -> (%d,%d)\n", width_, height_, width, height); fflush(stdout);
		if (width < width_) width = width_;
		if (height < height_) height = height_;
		///if (width == width_ && height == height_) return;
		CharRect old_cr (*this);
		free(bytes_);
		bytes_ = new char[width * height];
        width_ = width;
        height_ = height;
		clear(fill_);
		blit(&old_cr, 0, 0, 0, 0, -1, -1, true);
    }
protected:
	int index(int row, int col) const {
        return row*width_ + col;
	}
	int len() const {
		return width_ * height_;
	}
private:
    int width_;
    int height_;
	char* bytes_;
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
        if (fd == NULL) return false;
        std::list<std::string> rows;
        int max_len = 0;
        char curr_ch = '\0';
        char line[1024];
        while (fgets(line, sizeof(line), fd) != NULL) {
            char * const nl = strchr(line, '\n');
            if (nl != NULL) *nl = '\0';
			int kern = 0;
			if (hdr_line(line, &kern)) {
                if (rows.size() > 0) {
                    CharRect* cr = new CharRect (rows.size(), max_len, fill, kern);
                    cr->init(rows);
                    set_char_image(curr_ch, cr);
                    rows.clear();
                }
                max_len = 0;
                curr_ch = line[1];
            } else {
				max_len = std::max(max_len, (int) strlen(line));
                rows.push_back(std::string(line));
            }
        }
        fclose(fd);
        return true;
    }
	bool hdr_line(char const* line, int* kern) {
		if (line[0] != '=' || line[1] == '\0') return false;
		for (char const* p = &line[2]; ; ) {
			while (*p == ' ')
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
//        for (char const* p = "\33[H\33[J"; *p; ++p)
//            (*putc)(*p);
        int rows = std::min(img_.height(), sc_height-1);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < sc_width; ++col) {
                (*putc)(img_.get_at(row, col+offset));
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
    delay_time.tv_sec = delay_ms / 1000;
    delay_time.tv_nsec = (delay_ms % 1000) * 1000000;
    Font font (font_file);
    Banner banner(str, font);
    for (int offset = 0;; offset += offset_incr) {
        banner.print(offset, sc_width, sc_height, putch);
        nanosleep(&delay_time, NULL);
    }
}

int main(int argc, char** argv) {
    int sc_width = atoi(getenv("COLUMNS"));
    int sc_height = atoi(getenv("LINES"));
    int delay = 100;
    int offset_incr = 1;
    int space = 3;
    int fill = ' ';
    std::string font_file = "?";
    int ch;
    while ((ch = getopt(argc, argv, "d:f:F:h:i:s:w:")) != -1) {
        switch (ch) {
        case 'd':
            delay = atoi(optarg);
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
    run_banner(str, font_file, delay, offset_incr, sc_width, sc_height, space, fill);
    return 0;
}
