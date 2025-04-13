/*
 * machine.c
 *
 * Renderer for Matt Keeter's Prospero Challenge
 * https://www.mattkeeter.com/projects/prospero/
 *
 * Simeon Veldstra, 2025
 *
 */

#include "machine.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h> 
#include <time.h>
#include <pthread.h>

#define START_TIMER clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
#define PRINT_TIMER clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time); \
printf("cpu time: %f", (end_time.tv_sec - start_time.tv_sec) + ((end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0));

/* Render the image. Options in machine.h */ 
int main(int argc, char** argv) {
	struct timespec start_time, end_time;
	int opcount;

	fp_type* space = linspace(IMAGE_SIZE);
	START_TIMER
	func sdf = parse_file(FILENAME);
	PRINT_TIMER
	printf("\n");
	
	printf("Constant folding is %s, ", FOLD_CONST ? "enabled" : "disabled");
	if (FOLD_CONST) {
		START_TIMER
		opcount = fold_const(&sdf);
		printf("converted %d operations to const loads, ", opcount);
		PRINT_TIMER
	}
	printf("\n");

	printf("Const instruction removal is %s, ", CUT_CONST ? "enabled" : "disabled");
	if (CUT_CONST) {
		START_TIMER
		opcount = cut_const(&sdf);
		printf("removed %d const instructions from program, ", opcount);
		PRINT_TIMER
	}
	printf("\n");

	int data_size = sizeof(char) * IMAGE_SIZE * IMAGE_SIZE;

	char * data = (char *) malloc(data_size);
	if (!data) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(1);
	}
	
	int num_threads = NUM_THREADS;
	printf("Starting render... ");
	if (num_threads) printf("spawning %d threads. ", num_threads);
	printf("\n");

	START_TIMER
	if (num_threads) {
		pthread_t threads[num_threads];
		chunk_args args[num_threads];
		int chunk_size = (data_size / num_threads);  
		int i = 0;

		for (i=0; i < (num_threads - 1); i++) { 
			args[i].sdf = &sdf;
			args[i].startidx = chunk_size * i;
			args[i].size = chunk_size;
			args[i].stride = IMAGE_SIZE;
			args[i].data = data;
			args[i].space = space;
			if (pthread_create(&threads[i], NULL, start_thread, &args[i])) {
				fprintf(stderr, "Problem creating thread\n");
				exit(1);
			}
		}
		args[i].sdf = &sdf;
		args[i].startidx = (num_threads - 1) * chunk_size;
		args[i].size = data_size - args[i].startidx;
		args[i].stride = IMAGE_SIZE;
		args[i].data = data;
		args[i].space = space;
		if (pthread_create(&threads[i], NULL, start_thread, &args[i])) {
			fprintf(stderr, "Problem creating last thread\n");
			exit(1);
		}

		// Wait
		for (i=0; i < num_threads; i++) {
			if (pthread_join(threads[i], NULL)) {
				fprintf(stderr, "Problem joining threads\n");
				exit(1);
			}
		}

	} else {
		render_chunk(&sdf, 0, data_size, IMAGE_SIZE, data, space);
	}
	PRINT_TIMER
	printf("\n ");

	write_ppm(OUTFILE, data, data_size);

	free(data);
	free(space);
	free(sdf.func);
	free(sdf.constfree);
	return 0;
}


/* Opens filename and parses instructions out of it.
 *
 * Returns a func structure containing an array of operations and its length.
 * Caller must free the operation array inside the func. 
 *
 * Warning: an attacker able to provide arbitrary input to this function may be able
 * to craft an input string that causes this function to hack your Amazon account 
 * and express order the entire Herb Schildt reference library. */
func parse_file(const char* filename) {
	func ret;
	ret.size = 0;
	ret.constfree = (operation *)0;
	ret.constfreesize = 0;
	int linecount = 0;
	int c, ok;
	char line[255];
	operation * oper;
	FILE* input = fopen(filename, "r");
	if (!input) {
		fprintf(stderr, "File opening failed\n");
		exit(1);
	}
	while ((c = fgetc(input)) != EOF) {
		if (c == '\n') linecount++;
	}
	printf("Parsing file: %s, file linecount: %d, ", filename, linecount);

	fseek(input, 0, SEEK_SET);

	ret.func = (operation *) malloc(sizeof(operation) * (linecount + 1));
	if (!ret.func) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(1);
	}
	oper = ret.func;

	while (EOF != (fscanf(input, "%254[^\n]\n", line))) {
		ok = parse_line(line, oper);
		if (ok) {
			ret.size += ok;
			oper += ok;
		}
	}

	printf("instruction count: %d, ", ret.size);
	return ret;
}


/* Parse one instruction in a C string, and store the operation in op 
 * Returns the number of instructions processed. */
int parse_line(const char* line, operation* op) {
	if ((line[0] == '#') | (line[0] =='\n')) return 0;
	
	int ok, lno, a, b;
	char opname[8], args[16];
	fp_type cval;

	ok = sscanf(line, "_%x %s %15[^\n]\n", &lno, opname, args);
	if ((ok != 3)&&(ok != 2)) goto error;

	if (strcmp(opname, "var-x")==0) {
		op->code = VAR_X;
		op->line = lno;
		return 1;
	}
	if (strcmp(opname, "var-y")==0) {
		op->code = VAR_Y;
		op->line = lno;
		return 1;
	}
	if (strcmp(opname, "const")==0) {
		op->code = CONST;
		op->line = lno;
		cval = strto_fp(args, NULL);  
		op->value = cval;
		return 1;
	}
	if (strcmp(opname, "square")==0) {
		op->code = SQUARE;
		op->line = lno;
		ok = sscanf(args, "_%x", &a);
		if (ok != 1) goto error;
		op->a = a;
		return 1;
	}
	if (strcmp(opname, "sqrt")==0) {
		op->code = SQRT;
		op->line = lno;
		ok = sscanf(args, "_%x", &a);
		if (ok != 1) goto error;
		op->a = a;
		return 1;
	}
	if (strcmp(opname, "neg")==0) {
		op->code = NEG;
		op->line = lno;
		ok = sscanf(args, "_%x", &a);
		if (ok != 1) goto error;
		op->a = a;
		return 1;
	}
	if (strcmp(opname, "add")==0) {
		op->code = ADD;
		op->line = lno;
		ok = sscanf(args, "_%x _%x", &a, &b);
		if (ok != 2) goto error;
		op->a = a;
		op->b = b;
		return 1;
	}
	if (strcmp(opname, "sub")==0) {
		op->code = SUB;
		op->line = lno;
		ok = sscanf(args, "_%x _%x", &a, &b);
		if (ok != 2) goto error;
		op->a = a;
		op->b = b;
		return 1;
	}
	if (strcmp(opname, "mul")==0) {
		op->code = MUL;
		op->line = lno;
		ok = sscanf(args, "_%x _%x", &a, &b);
		if (ok != 2) goto error;
		op->a = a;
		op->b = b;
		return 1;
	}
	if (strcmp(opname, "max")==0) {
		op->code = MAX;
		op->line = lno;
		ok = sscanf(args, "_%x _%x", &a, &b);
		if (ok != 2) goto error;
		op->a = a;
		op->b = b;
		return 1;
	}
	if (strcmp(opname, "min")==0) {
		op->code = MIN;
		op->line = lno;
		ok = sscanf(args, "_%x _%x", &a, &b);
		if (ok != 2) goto error;
		op->a = a;
		op->b = b;
		return 1;
	}

error:
	fprintf(stderr, "Unable to parse line: %s\n", line);
	return 0;
}


void *start_thread(void * args_in) {
	chunk_args * args = (chunk_args *)args_in;
	render_chunk(args->sdf, args->startidx, args->size, args->stride, args->data, args->space);
	return (void *)0;
}


int render_chunk(func *sdf, int startidx, int size, int stride, char *data, fp_type *space) {

	quadresult fourpix;
	fp_type * scratch4;
	fp_type * scratch;
	scratch4 = (fp_type *) malloc(sizeof(fp_type) * sdf->size * 4 + (5 * sizeof(fp_type)));
	scratch = (fp_type *) malloc(sizeof(fp_type) * sdf->size + (2 * sizeof(fp_type)));
	if (!(scratch && scratch4)) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(1);
	}
	scratch4[sdf->size * 4 + 4] = 0.0; // Be sure the sentinel value for cut const is not set.
	scratch[sdf->size + 1] = 0.0;

	int xstart = startidx / stride;
	int xfin = xstart + (size / stride);
	for (int x=xstart; x <= xfin; x++) {
		int y;
		for (y=0; y + 4 <= stride; y += 4) {
			render_four_pixels(sdf, scratch4, space[x], -space[y], -space[y+1], -space[y+2], -space[y+3], &fourpix);
			data[(y+0)*stride+(x)] = (fourpix.one   < 0) ? 255 : 0;
			data[(y+1)*stride+(x)] = (fourpix.two   < 0) ? 255 : 0;
			data[(y+2)*stride+(x)] = (fourpix.three < 0) ? 255 : 0;
			data[(y+3)*stride+(x)] = (fourpix.four  < 0) ? 255 : 0;
		}
		for (; y < stride; y++) {
			data[y*stride+x] = (render_pixel(sdf, scratch, space[x], -space[y]) < 0) ? 255 : 0;
		}
	}

	free(scratch4);
	free(scratch);
	return 0;
}


/* Process one pixel using block of memory for intermediate results
 *
 * memory is an array of fp_type sized max(greatest func->line value, length of func)
 * Since the language has no control flow, and every statement is an assignment, 
 * we know we will need no more than one cell per instruction. We do the simplest thing, 
 * and allocate a scratch space that size.
 * Registers for everyone!
 */
fp_type render_pixel(func *sdf, fp_type* memory, fp_type x, fp_type y) {
	operation* function = sdf->func; 
	operation* funcbase = sdf->func;
	int size = sdf->size;
	fp_type out;

	// Only execute the const instructions on the first time through the block of memory. 
	if ((memory[sdf->size + 1] == BEEN_INITIALIZED) && CUT_CONST) {
		funcbase = sdf->constfree;
		size = sdf->constfreesize;
	}

	for (int i=0; i < size; i++) {
		function = funcbase +i;
		switch (function->code) {
			case VAR_X:
				memory[function->line] = x;
				break;
			case VAR_Y:
				memory[function->line] = y;
				break;
			case CONST:
				memory[function->line] = function->value;
				break;
			case NEG:
				memory[function->line] = -memory[function->a];
				break;
			case SQUARE:
				memory[function->line] = memory[function->a] * memory[function->a];
				break;
			case SQRT:
				memory[function->line] = sqrt_fp(memory[function->a]);
				break;
			case ADD:
				memory[function->line] = memory[function->a] + memory[function->b];
				break;
			case SUB:
				memory[function->line] = memory[function->a] - memory[function->b];
				break;
			case MUL:
				memory[function->line] = memory[function->a] * memory[function->b];
				break;
			case MAX:
				memory[function->line] = fmax_fp(memory[function->a], memory[function->b]);
				break;
			case MIN:
				memory[function->line] = fmin_fp(memory[function->a], memory[function->b]);
				break;
		}
	}
	out = memory[function->line];
	memory[sdf->size + 1] = BEEN_INITIALIZED;
	return out;
}

/* Process four pixels at once while chanting rhythmically in an interleaved scratch space in an attempt to summon the SIMDemon. 
 * Don't forget to sacrifice four times the customary memory for intermediates. */
int render_four_pixels(func *sdf, fp_type* memory, fp_type x, fp_type y1, fp_type y2, fp_type y3, fp_type y4, quadresult * out) {
	operation* function = sdf->func; 
	operation* funcbase = sdf->func;
	int size = sdf->size;

	// Only execute the const instructions on the first time through. 
	if ((memory[sdf->size * 4 + 4] == BEEN_INITIALIZED) && CUT_CONST)  {
		funcbase = sdf->constfree;
		size = sdf->constfreesize;
	} 
	for (int i=0; i < size; i++) {
		function = funcbase +i;
		switch (function->code) {
			case VAR_X:
				memory[(function->line * 4) + 0] = x;
				memory[(function->line * 4) + 1] = x;
				memory[(function->line * 4) + 2] = x;
				memory[(function->line * 4) + 3] = x;
				break;
			case VAR_Y:
				memory[(function->line * 4) + 0] = y1;
				memory[(function->line * 4) + 1] = y2;
				memory[(function->line * 4) + 2] = y3;
				memory[(function->line * 4) + 3] = y4;
				break;
			case CONST:
				memory[(function->line * 4) + 0] = function->value;
				memory[(function->line * 4) + 1] = function->value;
				memory[(function->line * 4) + 2] = function->value;
				memory[(function->line * 4) + 3] = function->value;
				break;
			case NEG:
				memory[(function->line * 4) + 0] = -memory[(function->a * 4) + 0];
				memory[(function->line * 4) + 1] = -memory[(function->a * 4) + 1];
				memory[(function->line * 4) + 2] = -memory[(function->a * 4) + 2];
				memory[(function->line * 4) + 3] = -memory[(function->a * 4) + 3];
				break;
			case SQUARE:
				memory[(function->line * 4) + 0] = memory[(function->a * 4) + 0] * memory[4*function->a + 0];
				memory[(function->line * 4) + 1] = memory[(function->a * 4) + 1] * memory[4*function->a + 1];
				memory[(function->line * 4) + 2] = memory[(function->a * 4) + 2] * memory[4*function->a + 2];
				memory[(function->line * 4) + 3] = memory[(function->a * 4) + 3] * memory[4*function->a + 3];
				break;
			case SQRT:
				memory[(function->line * 4) + 0] = sqrt_fp(memory[(function->a * 4) + 0]);
				memory[(function->line * 4) + 1] = sqrt_fp(memory[(function->a * 4) + 1]);
				memory[(function->line * 4) + 2] = sqrt_fp(memory[(function->a * 4) + 2]);
				memory[(function->line * 4) + 3] = sqrt_fp(memory[(function->a * 4) + 3]);
				break;
			case ADD:
				memory[(function->line * 4) + 0] = memory[(function->a * 4) + 0] + memory[(function->b * 4) + 0];
				memory[(function->line * 4) + 1] = memory[(function->a * 4) + 1] + memory[(function->b * 4) + 1];
				memory[(function->line * 4) + 2] = memory[(function->a * 4) + 2] + memory[(function->b * 4) + 2];
				memory[(function->line * 4) + 3] = memory[(function->a * 4) + 3] + memory[(function->b * 4) + 3];
				break;
			case SUB:
				memory[(function->line * 4) + 0] = memory[(function->a * 4) + 0] - memory[(function->b * 4) + 0];
				memory[(function->line * 4) + 1] = memory[(function->a * 4) + 1] - memory[(function->b * 4) + 1];
				memory[(function->line * 4) + 2] = memory[(function->a * 4) + 2] - memory[(function->b * 4) + 2];
				memory[(function->line * 4) + 3] = memory[(function->a * 4) + 3] - memory[(function->b * 4) + 3];
				break;
			case MUL:
				memory[(function->line * 4) + 0] = memory[(function->a * 4) + 0] * memory[(function->b * 4) + 0];
				memory[(function->line * 4) + 1] = memory[(function->a * 4) + 1] * memory[(function->b * 4) + 1];
				memory[(function->line * 4) + 2] = memory[(function->a * 4) + 2] * memory[(function->b * 4) + 2];
				memory[(function->line * 4) + 3] = memory[(function->a * 4) + 3] * memory[(function->b * 4) + 3];
				break;
			case MAX:
				memory[(function->line * 4) + 0] = fmax_fp(memory[(function->a * 4) + 0], memory[(function->b * 4) + 0]);
				memory[(function->line * 4) + 1] = fmax_fp(memory[(function->a * 4) + 1], memory[(function->b * 4) + 1]);
				memory[(function->line * 4) + 2] = fmax_fp(memory[(function->a * 4) + 2], memory[(function->b * 4) + 2]);
				memory[(function->line * 4) + 3] = fmax_fp(memory[(function->a * 4) + 3], memory[(function->b * 4) + 3]);
				break;
			case MIN:
				memory[(function->line * 4) + 0] = fmin_fp(memory[(function->a * 4) + 0], memory[(function->b * 4) + 0]);
				memory[(function->line * 4) + 1] = fmin_fp(memory[(function->a * 4) + 1], memory[(function->b * 4) + 1]);
				memory[(function->line * 4) + 2] = fmin_fp(memory[(function->a * 4) + 2], memory[(function->b * 4) + 2]);
				memory[(function->line * 4) + 3] = fmin_fp(memory[(function->a * 4) + 3], memory[(function->b * 4) + 3]);
				break;
		}
	}
	out->one   = memory[(function->line * 4) + 0];
	out->two   = memory[(function->line * 4) + 1];
	out->three = memory[(function->line * 4) + 2];
	out->four  = memory[(function->line * 4) + 3];
	memory[(sdf->size * 4) + 4] = BEEN_INITIALIZED; 
	return 0;
}


/* write out an 8-bit ppm image file */
int write_ppm(const char * filename, char * data, int size) {
	FILE * fp = fopen(filename, "wb");
	fprintf(fp, "P5\n%d %d\n255\n", IMAGE_SIZE, IMAGE_SIZE);
	fwrite(data, size, 1, fp);
	fclose(fp);
	return 1;
}


/* Chop up the -1/1 space into an array of size evenly spaced floats. 
 * Caller must free memory */
fp_type* linspace(int size) {
	fp_type step = 2.0 / size;
	fp_type* out = (fp_type*)  malloc(sizeof(fp_type) * size + 1);
	if (!out) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(1);
	}
	out[0] = -1;
	for (int i=1; i<=size; i++) {
		out[i] = out[i-1] + step;
	}
	return out;
}


/* Make a copy of the function with const operations removed. */
int cut_const(func *sdf) {
	sdf->constfree = (operation *) malloc(sizeof(operation) * (sdf->size + 1));
	if (!sdf->constfree) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(1);
	}

	operation *output = sdf->constfree;
	for (int i = 0; i < sdf->size; i++) {
		if (sdf->func[i].code != CONST) {
			memcpy(output, &sdf->func[i], sizeof(operation));
			output += 1;
			sdf->constfreesize += 1;

		}
	}

	sdf->constfree = (operation *) realloc(sdf->constfree, sizeof(operation) * sdf->constfreesize);
	return sdf->size - sdf->constfreesize;
}


/* fold constants in one operator and its operand(s) recursively. */
int fold_const_operator(func *sdf, operation *oper) {
	int count = 0;
	int a, b;
	operation *func = sdf->func;

	switch (oper->code) {
		case VAR_X:
		case VAR_Y:
		case CONST:
			return count;
		case NEG:
			a = oper->a;
			count += fold_const_operator(sdf, func+a);
			if (sdf->func[a].code == CONST) {
				printf("found NEG\t");
				oper->code = CONST;
				oper->value = -(func[a].value);
				return count + 1;
			}
			return count;
		case SQUARE:
			a = oper->a;
			count += fold_const_operator(sdf, func+a);
			if (sdf->func[a].code == CONST) {
				printf("found SQUARE\t");
				oper->code = CONST;
				oper->value = sdf->func[a].value * sdf->func[a].value;
				return count + 1;
			}
			return count;
		case SQRT:
			a = oper->a;
			count += fold_const_operator(sdf, func+a);
			if ((func+a)->code == CONST) {
				printf("found SQRT\t");
				oper->code = CONST;
				oper->value = sqrt_fp(sdf->func[a].value);
				return count + 1;
			}
			return count;
		case ADD:
			a = oper->a;
			b = oper->b;
			count += fold_const_operator(sdf, &sdf->func[a]);
			count += fold_const_operator(sdf, &sdf->func[b]);
			printf("ADD: %d,%d\n", (func+a)->code, sdf->func[b].code);
			if ((sdf->func[a].code == CONST) && (sdf->func[b].code == CONST))  {
				printf("found ADD\t");
				oper->code = CONST;
				oper->value = sdf->func[a].value + sdf->func[b].value;
				return count + 1;
			}
			return count;
		case SUB:
			a = oper->a;
			b = oper->b;
			count += fold_const_operator(sdf, &sdf->func[a]);
			count += fold_const_operator(sdf, &sdf->func[b]);
			if ((sdf->func[a].code == CONST) && (sdf->func[b].code == CONST))  {
				printf("found SUB\t");
				oper->code = CONST;
				oper->value = sdf->func[a].value - sdf->func[b].value;
				return count + 1;
			}
			return count;
		case MUL:
			a = oper->a;
			b = oper->b;
			count += fold_const_operator(sdf, &sdf->func[a]);
			count += fold_const_operator(sdf, &sdf->func[b]);
			if ((sdf->func[a].code == CONST) && (sdf->func[b].code == CONST))  {
				printf("found MUL\t");
				oper->code = CONST;
				oper->value = sdf->func[a].value * sdf->func[b].value;
				return count + 1;
			}
			return count;
		case MAX:
			a = oper->a;
			b = oper->b;
			count += fold_const_operator(sdf, &sdf->func[a]);
			count += fold_const_operator(sdf, &sdf->func[b]);
			if ((sdf->func[a].code == CONST) && (sdf->func[b].code == CONST))  {
				printf("found MAX\t");
				oper->code = CONST;
				oper->value = fmax_fp(sdf->func[a].value, sdf->func[b].value);
				return count + 1;
			}
			return count;
		case MIN:
			a = oper->a;
			b = oper->b;
			count += fold_const_operator(sdf, &sdf->func[a]);
			count += fold_const_operator(sdf, &sdf->func[b]);
			if ((sdf->func[a].code == CONST) && (sdf->func[b].code == CONST))  {
				printf("found MIN\t");
				oper->code = CONST;
				oper->value = fmin_fp(sdf->func[a].value, sdf->func[b].value);
				return count + 1;
			}
			return count;
	}
	fprintf(stderr, "Found bad opcode %d in constant folder\n", oper->code);
	return 0;
}


/* Fold constants. Modifies the funtion in-place by converting operations where all operands are 
 * constants into constants. */
int fold_const(func *sdf) {
	int count = 0;
	for (int i = 0; i < sdf->size; i++) {
		count += fold_const_operator(sdf, sdf->func + i);
	}
	return count;
}


