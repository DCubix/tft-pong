#define RGB(r, g, b) uint16_t((((31*((r)+4))/255)<<11) | (((63*((g)+2))/255)<<5) | ((31*((b)+4))/255))
#define RGBF(r, g, b) RGB(uint8_t((r)*255.0f)&0xFF, uint8_t((g)*255.0f)&0xFF, uint8_t((b)*255.0f)&0xFF)

#define TRANSPARENT 0xF81F

#include <Adafruit_SPITFT.h>
#include <initializer_list>
#include <stdarg.h>

#include "pong_sprites.h"

#define CS 5
#define RS 19
#define A0 4

#define ST_CMD_DELAY 0x80 // special signifier for command lists

#define SWRESET 0x01
#define RDDID 0x04
#define RDDST 0x09
#define SLPIN 0x10
#define SLPOUT 0x11
#define PTLON 0x12
#define NORON 0x13
#define INVOFF 0x20
#define INVON 0x21
#define DISPOFF 0x28
#define DISPON 0x29
#define CASET 0x2A
#define RASET 0x2B
#define RAMWR 0x2C
#define RAMRD 0x2E
#define PTLAR 0x30
#define TEOFF 0x34
#define TEON 0x35
#define MADCTL 0x36
#define COLMOD 0x3A
#define MADCTL_BGR 0x08
#define MADCTL_MH 0x04
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_ML 0x10
#define FRMCTR1 0xB1
#define FRMCTR2 0xB2
#define FRMCTR3 0xB3
#define INVCTR 0xB4
#define DISSET5 0xB6
#define PWCTR1 0xC0
#define PWCTR2 0xC1
#define PWCTR3 0xC2
#define PWCTR4 0xC3
#define PWCTR5 0xC4
#define VMCTR1 0xC5
#define PWCTR6 0xFC
#define GMCTRP1 0xE0
#define GMCTRN1 0xE1

#define SPI_DEFAULT_FREQ 36000000

class Display : public Adafruit_SPITFT {
public:
	Display(uint8_t cs, uint8_t dc, uint8_t rst)
		: Adafruit_SPITFT(128, 160, cs, dc, rst) 
	{
		m_buffer = (uint16_t*)malloc(_width * _height); // 16-bit color
		m_tileSet = NULL;
		memset(m_buffer, 0, _width * _height * 2);
	}

	~Display() {
		if (m_buffer) free(m_buffer);
	}

	void begin(uint32_t freq) {
		if (!freq) {
			freq = SPI_DEFAULT_FREQ;
		}
		_freq = freq;

		invertOnCommand = INVON;
		invertOffCommand = INVOFF;

		initSPI(freq, SPI_MODE0);

		cmd(SWRESET, 150);
		cmd(SLPOUT, 255);

		cmd(FRMCTR1, { 0x01, 0x2C, 0x2D });
		cmd(FRMCTR2, { 0x01, 0x2C, 0x2D });
		cmd(FRMCTR3, { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D });

		cmd(INVCTR, { 0x07 });

		cmd(PWCTR1, { 0xA2, 0x02, 0x84 });
		cmd(PWCTR2, { 0xC5 });
		cmd(PWCTR3, { 0x0A, 0x00 });
		cmd(PWCTR4, { 0x8A, 0x2A });
		cmd(PWCTR5, { 0x8A, 0xEE });

		cmd(VMCTR1, { 0x0E });

		cmd(INVOFF);

		cmd(MADCTL, { 0xC8 });
		cmd(COLMOD, { 0x05 });

		cmd(CASET, { 0x00, 0x02, 0x00, 0x7F+0x02 });
		cmd(RASET, { 0x00, 0x01, 0x00, 0x9F+0x01 });

		cmd(GMCTRP1, { 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10 });
		cmd(GMCTRN1, { 0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10 });

		cmd(NORON, 10);
		cmd(DISPON, 100);

		setRotation(0);
		flip();
	}

	void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
		uint32_t xa = ((uint32_t)x << 16) | (x + w - 1);
		uint32_t ya = ((uint32_t)y << 16) | (y + h - 1);

		writeCommand(CASET); // Column addr set
		SPI_WRITE32(xa);

		writeCommand(RASET); // Row addr set
		SPI_WRITE32(ya);

		writeCommand(RAMWR);
	}

	void clear(uint16_t color = 0x0000) {
		for (uint16_t i = 0; i < _width * _height; i++) m_buffer[i] = color;
	}

	void pix(int16_t x, int16_t y, uint16_t color) {
		if (!m_buffer) return;
		if (x < 0 || x >= _width || y < 0 || y >= _height) return;
		if (color == TRANSPARENT) return;
		m_buffer[x + y * _width] = color;
	}

	uint16_t get(int16_t x, int16_t y) {
		if (x < 0 || x >= _width || y < 0 || y >= _height) return 0;
		return m_buffer[x + y * _width];
	}

	void line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
		int16_t dx = abs(x2 - x1);
		int16_t sx = x1 < x2 ? 1 : -1;
		int16_t dy = -abs(y2 - y1);
		int16_t sy = y1 < y2 ? 1 : -1;
		int16_t err = dx + dy;
		int16_t e2 = 0;

		int16_t x = x1;
		int16_t y = y1;

		while (true) {
			pix(x, y, color);
			if (x == x2 && y == y2) break;
			e2 = 2 * err;
			if (e2 >= dy) { err += dy; x += sx; }
			if (e2 <= dx) { err += dx; y += sy; }
		}
	}

	void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
		line(x, y, x+w, y, color);
		line(x, y+h, x+w, y+h, color);
		line(x, y, x, y+h, color);
		line(x+w, y, x+w, y+h, color);
	}

	void tile(int index, int16_t x, int16_t y, bool vflip = false, bool hflip = false) {
		if (!m_tileSet) return;
		const int16_t sx = (index % m_tileSetCols) * 8;
		const int16_t sy = (index / m_tileSetCols) * 8;

		for (int16_t ty = 0; ty < 8; ty++) {
			for (int16_t tx = 0; tx < 8; tx++) {
				pix(tx + x, ty + y, tileSetGet(sx + (hflip ? (7-tx) : tx), sy + (vflip ? (7-ty) : ty)));
			}
		}
	}

	void tileColored(int index, int16_t x, int16_t y, uint16_t color, bool vflip = false, bool hflip = false) {
		if (!m_tileSet) return;
		const int16_t sx = (index % m_tileSetCols) * 8;
		const int16_t sy = (index / m_tileSetCols) * 8;

		for (int16_t ty = 0; ty < 8; ty++) {
			for (int16_t tx = 0; tx < 8; tx++) {
				uint16_t col = tileSetGet(sx + (hflip ? (7-tx) : tx), sy + (vflip ? (7-ty) : ty));
				pix(tx + x, ty + y, col != TRANSPARENT ? color : TRANSPARENT);
			}
		}
	}

	void tileRect(int16_t x, int16_t y, uint16_t sx, uint16_t sy, uint16_t sw, uint16_t sh) {
		if (!m_tileSet) return;
		for (int16_t ty = 0; ty < sh; ty++) {
			for (int16_t tx = 0; tx < sw; tx++) {
				tile((sx+tx) + (sy+ty) * m_tileSetCols, x + tx * 8, y + ty * 8);
			}
		}
	}

	void bitmap(uint16_t w, uint16_t h, uint16_t* buff) {
		startWrite();
		setAddrWindow(0, 0, w, h);
		writePixels(buff, w * h);
		endWrite();
	}

	void flip() {
		startWrite();
		setAddrWindow(0, 0, _width, _height);
		writePixels(m_buffer, _width * _height);
		endWrite();
	}

	void setRotation(uint8_t m) {
		Adafruit_SPITFT::setRotation(m);
		uint8_t madctl = 0;
		
		int16_t x = 2;
		int16_t y = rotation < 2 ? 3 : 1;

		switch (rotation) {
			case 0: madctl = MADCTL_MX | MADCTL_MY | MADCTL_BGR; _width = 128; _height = 160; _xstart = x; _ystart = y; break;
			case 1: madctl = MADCTL_MY | MADCTL_MV | MADCTL_BGR; _width = 160; _height = 128;  _xstart = y; _ystart = x; break;
			case 2: madctl = MADCTL_BGR; _width = 128; _height = 160; _xstart = x; _ystart = y; break;
			case 3: madctl = MADCTL_MX | MADCTL_MV | MADCTL_BGR; _width = 160; _height = 128; _xstart = y; _ystart = x; break;
		}

		cmd(MADCTL, { madctl });
	}

	void loadTileSet(const uint16_t* bitmap, uint16_t w, uint16_t h) {
		m_tileSet = (uint16_t*) bitmap;
		m_tileSetWidth = w;
		m_tileSetHeight = h;
		m_tileSetCols = w / 8;
		m_tileSetRows = h / 8;
	}

	uint16_t tileSetGet(int16_t x, int16_t y) {
		if (!m_tileSet) return 0;
		if (x < 0 || x >= m_tileSetWidth || y < 0 || y >= m_tileSetHeight) return 0;
		return m_tileSet[x + y * m_tileSetWidth];
	}

private:
	uint16_t* m_buffer;

	uint16_t* m_tileSet;
	uint16_t m_tileSetWidth, m_tileSetHeight, m_tileSetCols, m_tileSetRows;

	void cmd(uint8_t cmd, std::initializer_list<uint8_t> args, uint16_t ms = 0) {
		sendCommand(cmd, args.begin(), args.size());
		if (ms) delay(ms);
	}

	void cmd(uint8_t cmd, uint16_t ms = 0) {
		sendCommand(cmd);
		if (ms) delay(ms);
	}

};

Display d = Display(CS, A0, RS);

enum GameState {
	GS_MENU = 0,
	GS_GAME_TRANSITION,
	GS_GAME,
	GS_GAMEOVER,
	GS_HISCORES
};

struct vec2 {
	float x, y;
	vec2() : x(0.0f), y(0.0) {}
	vec2(float x, float y) : x(x), y(y) {}
	vec2 operator +(const vec2& o) { return vec2(x + o.x, y + o.y); }
	vec2& operator +=(const vec2& o) { x += o.x; y += o.y; return *this; }
	vec2 operator *(float o) { return vec2(x * o, y * o); }
};

#define FIRE 12
#define AXIS 34
#define START_TEXT0 "PRESS [FIRE]"
#define START_TEXT1 "TO START!"
#define BALL_TILE 99
#define PAD_TILE 96
#define FADE_TILE 101
#define PAD_WIDTH 24

#define LRP(a, b, t) (1.0f - (t)) * (a) + (b) * (t);

int16_t game_draw_char(char c, int16_t x, int16_t y) {
	c = c & 0x7F;
	if (c < ' ') {
		c = 0;
	} else {
		c -= ' ';
	}
	d.tile(c, x, y);
	return x + 7;
}

void game_draw_text(const char* str, int16_t x, int16_t y) {
	uint16_t len = strlen(str);
	uint16_t i = 0;
	while (len--) x = game_draw_char(str[i++], x, y);
}

void game_draw_textf(int16_t x, int16_t y, const char* fmt, ...) {
	char buf[256];
	memset(buf, 0, sizeof(char)*256);

	va_list lst;
	va_start(lst, fmt);
	vsprintf(buf, fmt, lst);
	va_end(lst);
	game_draw_text(buf, x, y);
}

int16_t game_text_width(const char* str) {
	return (strlen(str)-1) * 7;
}

void game_draw_pad(int16_t x, int16_t y) {
	x -= 4;
	d.tile(PAD_TILE+0, x - 8, y - 3);
	d.tile(PAD_TILE+1, x, y - 3);
	d.tile(PAD_TILE+2, x + 8, y - 3);
}

int randRange(int rmin, int rmax) {
	return rmin + (rand() % (rmax - rmin));
}

float randf() {
	return float(rand() % RAND_MAX) / RAND_MAX;
}

GameState state = GS_MENU;

#define TS (1.0f/60.0f)
#define PONG_Y_END 32.0f
#define PONG_T 0.1f
float pong_y = -64.0f;

struct Contact {
	enum {
		CT_NONE = 0,
		CT_TOP,
		CT_MIDDLE,
		CT_BOTTOM,
		CT_LEFT,
		CT_RIGHT
	} type{ CT_NONE };
	float depth{ 0.0f };
};

struct Paddle {
	vec2 position;
	vec2 size;

	Paddle() = default;
	Paddle(const vec2& pos, const vec2& size) : position(pos), size(size) {}
};

struct Ball {
	vec2 position, velocity;

	int16_t x() const { return int16_t(position.x); }
	int16_t y() const { return int16_t(position.y); }

	float speed;

	Contact hitWall() {
		Contact ct{};

		float bl = position.x - 3;
		float br = position.x + 3;
		float bt = position.y - 3;
		float bb = position.y + 3;

		if (bl < 0.0f) {
			ct.type = Contact::CT_LEFT;
		} else if (br > 128.0f) {
			ct.type = Contact::CT_RIGHT;
		} else if (bt < 0.0f) {
			ct.type = Contact::CT_TOP;
			ct.depth = -bt;
		} else if (bb > 128.0f) {
			ct.type = Contact::CT_BOTTOM;
			ct.depth = 128.0f - bb;
		}

		return ct;
	}

	Contact hitPaddle(const Paddle& p) {
		Contact ct{};

		float bl = position.x - 3;
		float br = position.x + 3;
		float bt = position.y - 3;
		float bb = position.y + 3;

		float hpw = p.size.x / 2.0f;
		float hph = p.size.y / 2.0f;

		float pl = p.position.x - hpw;
		float pr = p.position.x + hpw;
		float pt = p.position.y - hph;
		float pb = p.position.y + hph;

		if (bl >= pr) return ct;
		if (br <= pl) return ct;
		if (bt >= pb) return ct;
		if (bb <= pt) return ct;

		const float slice = p.size.x / 3.0f;
		float pru = pr - (2.0f * slice);
		float prm = pr - slice;

		if (velocity.y < 0.0f) {
			ct.depth = pb - bt;
		} else if (velocity.y > 0) {
			ct.depth = pt - bb;
		}

		if (br > pl && br < pru) {
			ct.type = Contact::CT_TOP;
		} else if (br > pru && br < prm) {
			ct.type = Contact::CT_MIDDLE;
		} else {
			ct.type = Contact::CT_BOTTOM;
		}

		return ct;
	}

	void resolveWallCollision(const Contact& ct) {
		if (ct.type == Contact::CT_LEFT || ct.type == Contact::CT_RIGHT) {
			position.x += ct.depth;
			velocity.x = -velocity.x;
		} else if (ct.type == Contact::CT_TOP) {
			position.x = 64.0f;
			position.y = 64.0f;
			velocity.x = (float(randRange(0, 1)) * 2.0f - 1.0f) * 0.75f;
			velocity.y = 1.0f;
		} else if (ct.type == Contact::CT_BOTTOM) {
			position.x = 64.0f;
			position.y = 64.0f;
			velocity.x = (float(randRange(0, 1)) * 2.0f - 1.0f) * 0.75f;
			velocity.y = -1.0f;
		}
	}

	void resolveMenuWallCollision(const Contact& ct) {
		if (ct.type == Contact::CT_LEFT || ct.type == Contact::CT_RIGHT) {
			position.x += ct.depth;
			velocity.x = -velocity.x;
		} else if (ct.type == Contact::CT_TOP || ct.type == Contact::CT_BOTTOM) {
			position.y += ct.depth;
			velocity.y = -velocity.y;
		}
	}

	void resolvePadCollision(const Contact& ct) {
		position.y += ct.depth;
		velocity.y = -velocity.y;
		if (ct.type == Contact::CT_TOP) {
			velocity.x = -0.75f;
		} else if (ct.type == Contact::CT_BOTTOM) {
			velocity.x = 0.75f;
		}
	}
};

Ball menu_ball;
Ball game_ball;
Paddle padPlayer, padOpponent;
int frame = 0;
uint16_t scorePlayer = 0, scoreOpponent = 0;
uint8_t lives = 3;

int16_t axisPrev = 0;
bool buttonPressed = false;
bool fadeIn = false;

void start_new_game() {
	frame = 0;

	scorePlayer = scoreOpponent = 0;

	game_ball.position.x = 64.0f;
	game_ball.position.y = 64.0f;
	game_ball.speed = 120.0f;
	game_ball.velocity.x = 0.0f;
	game_ball.velocity.y = -1.0f;

	padPlayer = Paddle(vec2(64, 120), vec2(PAD_WIDTH, 6));
	padOpponent = Paddle(vec2(64, 8), vec2(PAD_WIDTH, 6));

	lives = 3;
	state = GS_GAME_TRANSITION;
	fadeIn = false;
}

void draw_game() {
	d.clear(0x354B);

	d.rect(3, 3, 122, 122, 0xCCCC);
	d.rect(4, 4, 120, 120, 0xFFFF);
	d.rect(5, 5, 118, 118, 0xCCCC);

	d.line(5, 63, 123, 63, 0xCCCC);
	d.line(4, 64, 124, 64, 0xFFFF);
	d.line(5, 65, 123, 65, 0xCCCC);

	d.tile(BALL_TILE, game_ball.x() - 3, game_ball.y() - 3);

	game_draw_pad(int16_t(padPlayer.position.x), int16_t(padPlayer.position.y));
	game_draw_pad(int16_t(padOpponent.position.x), int16_t(padOpponent.position.y));

	int16_t w = game_text_width("AAAAAAA");
	game_draw_textf(123 - w, 56, "2P%04d", scoreOpponent);
	game_draw_textf(123 - w, 66, "1P%04d", scorePlayer);
}

void setup() {
	pinMode(FIRE, INPUT);

	Serial.begin(115200);
	delay(1000);

	d.begin(0);
	d.setRotation(2);
	d.loadTileSet(pong_sprites, 256, 64);

	d.clear(0x354B);
	
	menu_ball.position.x = 64.0f;
	menu_ball.position.y = 64.0f;
	menu_ball.speed = 200.0f;
	menu_ball.velocity.x = randf() * 2.0f - 1.0f;
	menu_ball.velocity.y = -1.0f;

	axisPrev = int16_t(map(4095 - analogRead(AXIS), 0, 4095, 0, 255));
}

void loop() {
	bool fire = (digitalRead(FIRE) == HIGH);
	if (fire && !buttonPressed) {
		buttonPressed = true;
	} else if (!fire && buttonPressed) {
		buttonPressed = false;
	}

	switch (state) {
		default: break;
		case GS_MENU: {
			d.clear(0x354B);
			for (uint16_t y = 0; y < 128; y+=8) {
				for (uint16_t x = 0; x < 128; x+=8) {
					d.tileColored(FADE_TILE+map(y/8, 0, 16, 0, 14), x, y, 0x1b69);
				}
			}

			const int ox = 32, oy = int(pong_y);
			
			if (oy >= int(PONG_Y_END)-1) {
				d.tile(BALL_TILE, menu_ball.x() - 3, menu_ball.y() - 3);

				game_draw_text(START_TEXT0, 64 - game_text_width(START_TEXT0) / 2, oy + 32);
				game_draw_text(START_TEXT1, 64 - game_text_width(START_TEXT1) / 2, oy + 40);
				
				menu_ball.position += menu_ball.velocity * menu_ball.speed * TS;

				Contact ct = menu_ball.hitWall();
				if (ct.type != Contact::CT_NONE) {
					menu_ball.resolveMenuWallCollision(ct);
				}
				
				if (buttonPressed) {
					start_new_game();
				}
			} else {
				pong_y = (1.0f - PONG_T) * pong_y + PONG_Y_END * PONG_T;
			}

			d.tileRect(ox, oy, 24, 5, 8, 3);

			d.flip();
			delay(16);
			frame++;
		} break;
		case GS_GAME_TRANSITION: {
			const int maxFrames = 14;
			if (frame >= 14 && !fadeIn) {
				fadeIn = true;
				frame = 0;
			}

			int fr = fadeIn ? 14 - frame : frame;

			if (fadeIn) draw_game();
			for (uint8_t y = 0; y < d.height()/8; y++) {
				for (uint8_t x = 0; x < d.width()/8; x++) {
					d.tileColored(FADE_TILE+fr, x*8, y*8, 0x0);
				}
			}
			d.flip();
			delay(16);
			if (frame++ >= maxFrames && fadeIn) {
				state = GS_GAME;
				frame = 0;
				fadeIn = false;
			}
		} break;
		case GS_GAME: {
			int16_t axisNow = int16_t(map(4095 - analogRead(AXIS), 0, 4095, 0, 255));
			int16_t axisDelta = axisNow - axisPrev;
			axisPrev = axisNow;

			Contact ct = game_ball.hitWall();
			if (ct.type != Contact::CT_NONE) {
				if (ct.type == Contact::CT_TOP) scorePlayer++;
				else if (ct.type == Contact::CT_BOTTOM) scoreOpponent++;
				game_ball.resolveWallCollision(ct);
			}

			ct = game_ball.hitPaddle(padPlayer);
			if (ct.type != Contact::CT_NONE) {
				game_ball.resolvePadCollision(ct);
			}

			ct = game_ball.hitPaddle(padOpponent);
			if (ct.type != Contact::CT_NONE) {
				game_ball.resolvePadCollision(ct);
			}

			game_ball.position += game_ball.velocity * game_ball.speed * TS;
			
			if (abs(axisDelta) > 0) padPlayer.position.x += float(axisDelta) * 25.0f * TS;

			// update opponent
			if (game_ball.position.y <= 70.0f) {
				float d = (game_ball.position.x - padOpponent.position.x) < 0 ? -1.0f : 1.0f;
				padOpponent.position.x += d * 80.0f * TS;
			}

			if (padPlayer.position.x - PAD_WIDTH/2 < 0) {
				padPlayer.position.x = PAD_WIDTH/2;
			} else if (padPlayer.position.x + PAD_WIDTH/2 >= 128) {
				padPlayer.position.x = 128 - PAD_WIDTH/2;
			}

			if (padOpponent.position.x - PAD_WIDTH/2 < 0) {
				padOpponent.position.x = PAD_WIDTH/2;
			} else if (padOpponent.position.x + PAD_WIDTH/2 >= 128) {
				padOpponent.position.x = 128 - PAD_WIDTH/2;
			}

			draw_game();

			// d.rect(
			// 	int16_t(padPlayer.position.x - padPlayer.size.x / 2),
			// 	int16_t(padPlayer.position.y - padPlayer.size.y / 2),
			// 	int16_t(padPlayer.size.x),
			// 	int16_t(padPlayer.size.y),
			// 	0xFF80
			// );

			d.flip();
			delay(16);
			frame++;
		} break;
	}

}