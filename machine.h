/*
 * machine.h
 *
 * Renderer for Matt Keeter's Prospero Challenge
 * https://www.mattkeeter.com/projects/prospero/
 *
 * Simeon Veldstra, 2025
 *
 */

#define IMAGE_SIZE 1024
#define FILENAME "prospero.vm"
#define OUTFILE "out.ppm"


// Only execute const loads on first run through scratch memory (0 to disable)
#define CUT_CONST 1

// Use double precision floating point
#define DOUBLE

#ifdef DOUBLE
typedef double fp_type;
#define strto_fp strtod
#define sqrt_fp sqrt
#define fmax_fp fmax
#define fmin_fp fmin
#endif

#define BEEN_INITIALIZED 1.0

enum opcode {VAR_X, VAR_Y, CONST, ADD, SUB, MUL, MAX, MIN, NEG, SQUARE, SQRT};

typedef struct {
	int line;
	enum opcode code;
	union {
		fp_type value;
		struct {int a; int b;};
	};
} operation;

typedef struct {
	int size;
	operation* func;
	int constfreesize;
	operation* constfree;
} func;

typedef struct {
	fp_type one, two, three, four;
} quadresult;

int parse_line(const char* line, operation* op);

func parse_file(const char* filename);

fp_type render_pixel(func sdf, fp_type* memory, fp_type x, fp_type y);

int render_four_pixels(func sdf, fp_type* memory, fp_type x, fp_type y1, fp_type y2, fp_type y3, fp_type y4, quadresult * out);

int write_ppm(const char * filename, char * data, int size);

fp_type* linspace(int size);

