// Calculator - basic and scientific

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* M_PI is not guaranteed under strict C11 */
#define CALC_PI 3.14159265358979323846

typedef enum {
	OP_NONE      = 0,
	OP_ADD       = 1,
	OP_SUBSTRACT = 2,
	OP_MULTIPLY  = 3,
	OP_DIVIDE    = 4,
	OP_EQUAL     = 5,
	OP_POWER     = 6
} Operation;

typedef struct {
	double acc;                 /* the accumulated result */
	char input[14];             /* the number being typed (digits and '.') */
	uint8_t inputActive;
	Operation operation;
	uint8_t sciPage;            /* 0 = basic keypad, 1 = scientific keypad */
	uint8_t degrees;            /* trigonometry in degrees (vs radians) */
	uint8_t error;              /* a math error is on the display */
	uint8_t _flashKey;
	uint32_t _flashUntil;   /* key highlight stays visible until this time */
	double _hist[3];            /* the last few results, shown as a tape */
	uint8_t _histN;
} Game12Memory;

static Game12Memory * const memory = (Game12Memory*)(&mainMemory.sharedMemory.memblocks[0]);

#define MAX_INPUT 12
#define RESULT_MAX 999999999999.0

/*
 * The RP2040 SDK printf has no %f support, so numbers are formatted by hand:
 * plain notation with up to 6 decimals for the everyday range, scientific
 * notation for everything bigger than 1e11 or smaller than 1e-6. The output
 * never exceeds 13 characters, which keeps the display inside its panel.
 */
static void FormatNumber(char * out, double v)
{
	if (isnan(v) || isinf(v))
	{
		strcpy(out, "ERROR");
		return;
	}
	if (v > RESULT_MAX) v = RESULT_MAX;
	if (v < -RESULT_MAX) v = -RESULT_MAX;
	double av = v < 0 ? -v : v;
	if (av != 0.0 && (av >= 1e11 || av < 1e-6))
	{
		/* scientific: one digit before the point, five after */
		int exp = 0;
		double m = av;
		while (m >= 10.0) { m /= 10.0; exp++; }
		while (m < 1.0)   { m *= 10.0; exp--; }
		long frac = (long)(m*100000.0 + 0.5);
		if (frac >= 1000000) { frac /= 10; exp++; }
		sprintf(out, "%s%ld.%05lde%d", (v < 0) ? "-" : "", frac/100000, frac%100000, exp);
		return;
	}
	long long ip = (long long)av;
	double frac = av - (double)ip;
	long fp = (long)(frac*1000000.0 + 0.5);
	if (fp >= 1000000)
	{
		fp -= 1000000;
		ip += 1;
	}
	const char * sign = (v < 0) ? "-" : "";
	if (fp == 0)
	{
		sprintf(out, "%s%lld", sign, ip);
		return;
	}
	/* trim trailing zeros of the fraction */
	int digits = 6;
	while (digits > 1 && (fp % 10) == 0) { fp /= 10; digits--; }
	/* keep the whole string within MAX_INPUT+1 characters */
	char head[24];
	int hl = sprintf(head, "%s%lld.", sign, ip);
	if (hl >= MAX_INPUT)
	{
		/* the integer part alone fills the display */
		sprintf(out, "%s%lld", sign, ip);
		return;
	}
	while (hl + digits > MAX_INPUT && digits > 1) { fp /= 10; digits--; }
	/* digits is always 1..6 here; the explicit clamp keeps the precision of
	   the %0*ld directive provably bounded (and silences -Wformat-overflow) */
	if (digits < 1) digits = 1;
	if (digits > 6) digits = 6;
	sprintf(out, "%s%0*ld", head, digits, fp);
}

static double CurrentValue(void)
{
	if (memory->inputActive)
		return strtod(memory->input, 0);
	return memory->acc;
}

static void SetError(void)
{
	memory->error = 1;
	memory->inputActive = 0;
	memory->input[0] = 0;
	memory->operation = OP_NONE;
	memory->acc = 0;
}

static void ClampAcc(void)
{
	if (isnan(memory->acc) || isinf(memory->acc))
	{
		SetError();
		return;
	}
	if (memory->acc > RESULT_MAX) memory->acc = RESULT_MAX;
	if (memory->acc < -RESULT_MAX) memory->acc = -RESULT_MAX;
}

static void G_Init(void)
{
    mainMemory._gameState = GS_RUNNING;
    memory->operation = OP_NONE;
    memory->acc = 0;
    memory->input[0] = 0;
    memory->inputActive = 0;
	memory->sciPage = 0;
	memory->degrees = 1;
	memory->error = 0;
	memory->_flashKey = 0;
	memory->_flashUntil = 0;
	memory->_histN = 0;
	Keyboard_GetPressedKeys();
	Touch_ClearTap();
}

static void NumPressed(char c)
{
	if (memory->error)
		return;
	/* typing a digit after "=" starts a new calculation instead of silently
	   reusing the old result */
	if (memory->operation == OP_EQUAL)
	{
		memory->operation = OP_NONE;
		memory->acc = 0;
	}
	if (!memory->inputActive)
	{
		memory->inputActive = 1;
		memory->input[0] = 0;
	}
	size_t len = strlen(memory->input);
	if (c == '.')
	{
		if (len == 0)
		{
			memory->input[0] = '0';
			memory->input[1] = 0;
			len = 1;
		}
		if (strchr(memory->input, '.'))
			return;
	}
	if (len < MAX_INPUT)
	{
		memory->input[len] = c;
		memory->input[len+1] = 0;
	}
}

static void BackspacePressed(void)
{
	if (!memory->inputActive)
		return;
	size_t len = strlen(memory->input);
	if (len)
		memory->input[len-1] = 0;
	if (!memory->input[0])
		memory->inputActive = 0;
}

static void OperationPressed(Operation op)
{
	if (memory->error)
		return;
	double input = CurrentValue();
	// first calculate whatever it was
	switch(memory->operation)
	{
	case OP_ADD:
		memory->acc += input;
		break;
	case OP_SUBSTRACT:
		memory->acc -= input;
		break;
	case OP_MULTIPLY:
		memory->acc *= input;
		break;
	case OP_DIVIDE:
		if (input == 0.0)
		{
			SetError();
			return;
		}
		memory->acc /= input;
		break;
	case OP_POWER:
		memory->acc = pow(memory->acc, input);
		break;
	case OP_EQUAL:
		memory->operation = OP_NONE;
		break;
	case OP_NONE:
	default:
		memory->acc = input;
		break;
	}
	ClampAcc();
	if (memory->error)
		return;
	/* remember the result of an "=" so the player can review recent sums */
	if (op == OP_EQUAL)
	{
		if (memory->_histN < 3)
			memory->_hist[memory->_histN++] = memory->acc;
		else
		{
			memory->_hist[0] = memory->_hist[1];
			memory->_hist[1] = memory->_hist[2];
			memory->_hist[2] = memory->acc;
		}
	}
	memory->operation = op;
	memory->input[0] = 0;
	memory->inputActive = 0;
}

/* unary scientific function applied to the number on the display: when an
   operand is being typed it transforms that operand, otherwise the result */
static void UnaryPressed(uint8_t fn)
{
	if (memory->error)
		return;
	double v = CurrentValue();
	double r = 0;
	switch (fn)
	{
	case 0: r = sqrt(v); break;                     /* sqrt */
	case 1: r = v*v; break;                         /* x^2 */
	case 2:                                         /* 1/x */
		if (v == 0.0) { SetError(); return; }
		r = 1.0/v;
		break;
	case 3: r = -v; break;                          /* +/- */
	case 4:                                         /* sin */
		r = sin(memory->degrees ? v*CALC_PI/180.0 : v);
		break;
	case 5:                                         /* cos */
		r = cos(memory->degrees ? v*CALC_PI/180.0 : v);
		break;
	case 6:                                         /* tan */
	{
		double rad = memory->degrees ? v*CALC_PI/180.0 : v;
		if (fabs(cos(rad)) < 1e-12) { SetError(); return; }
		r = tan(rad);
		break;
	}
	case 7:                                         /* ln */
		if (v <= 0.0) { SetError(); return; }
		r = log(v);
		break;
	case 8:                                         /* log10 */
		if (v <= 0.0) { SetError(); return; }
		r = log10(v);
		break;
	case 9: r = CALC_PI; break;                     /* pi */
	case 10: r = 2.718281828459045; break;          /* e */
	case 11: r = v/100.0; break;                    /* % */
	default: return;
	}
	if (isnan(r) || isinf(r))
	{
		SetError();
		return;
	}
	if (memory->inputActive && memory->operation != OP_EQUAL)
	{
		/* keep the transformed value as the operand being typed */
		FormatNumber(memory->input, r);
	}
	else
	{
		memory->acc = r;
		memory->inputActive = 0;
		memory->input[0] = 0;
		if (memory->operation == OP_EQUAL)
			memory->operation = OP_NONE;
		ClampAcc();
	}
}

#define BOX_W ((GPU_X/2)/4)
#define BOX_H (GPU_Y/4)

/* scientific keypad, indices 0..15 like the basic one */
static void PressSciKey(uint8_t pressed)
{
	memory->_flashKey = (uint8_t)(pressed + 1);
	memory->_flashUntil = IF_GetCurrentTime() + 200000;
	switch (pressed)
	{
	case 0:  UnaryPressed(0); break;                /* sqrt */
	case 1:  UnaryPressed(1); break;                /* x^2 */
	case 2:  OperationPressed(OP_POWER); break;     /* x^y */
	case 3:  UnaryPressed(2); break;                /* 1/x */
	case 4:  UnaryPressed(4); break;                /* sin */
	case 5:  UnaryPressed(5); break;                /* cos */
	case 6:  UnaryPressed(6); break;                /* tan */
	case 7:  UnaryPressed(3); break;                /* +/- */
	case 8:  UnaryPressed(7); break;                /* ln */
	case 9:  UnaryPressed(8); break;                /* log */
	case 10: UnaryPressed(9); break;                /* pi */
	case 11: UnaryPressed(10); break;               /* e */
	case 12: NumPressed('.'); break;                /* decimal point */
	case 13: UnaryPressed(11); break;               /* % */
	case 14: BackspacePressed(); break;             /* backspace */
	case 15: memory->sciPage = 0; break;            /* back to basic */
	default: break;
	}
}

static void PressKey(uint8_t pressed)
{
	memory->_flashKey = (uint8_t)(pressed + 1);
	memory->_flashUntil = IF_GetCurrentTime() + 200000;
	switch (pressed)
	{
	case 0:  NumPressed('1'); break;
	case 1:  NumPressed('2'); break;
	case 2:  NumPressed('3'); break;
	case 3:  OperationPressed(OP_ADD); break;
	case 4:  NumPressed('4'); break;
	case 5:  NumPressed('5'); break;
	case 6:  NumPressed('6'); break;
	case 7:  OperationPressed(OP_SUBSTRACT); break;
	case 8:  NumPressed('7'); break;
	case 9:  NumPressed('8'); break;
	case 10: NumPressed('9'); break;
	case 11: OperationPressed(OP_MULTIPLY); break;
	case 12: NumPressed('.'); break;
	case 13: NumPressed('0'); break;
	case 14: OperationPressed(OP_DIVIDE); break;
	case 15: OperationPressed(OP_EQUAL); break;
	default: break;
	}
}

/* the three small buttons under the display */
#define CLR_BTN_X (GPU_X/2+8)
#define SCI_BTN_X (GPU_X/2+56)
#define DEG_BTN_X (GPU_X/2+104)
#define SCI_BTN_Y 124
#define SCI_BTN_W 44
#define SCI_BTN_H 22

static void G_Update(void)
{
	if (memory->_flashKey && (int32_t)(IF_GetCurrentTime() - memory->_flashUntil) >= 0)
		memory->_flashKey = 0;
    // get input
	if (mainMemory.touchPressedNew)
    {
		uint16_t tx = mainMemory.touch_X, ty = mainMemory.touch_Y;
        if (tx < GPU_X/2 && ty < GPU_Y)
		{
			uint8_t x = (uint8_t)(tx/BOX_W);
			uint8_t y = (uint8_t)(ty/BOX_H);
			if (x < 4 && y < 4)
			{
				uint8_t key = (uint8_t)(4*y + x);
				if (memory->sciPage)
					PressSciKey(key);
				else
					PressKey(key);
			}
		}
		else if (ty >= SCI_BTN_Y && ty < SCI_BTN_Y+SCI_BTN_H)
		{
			if (tx >= CLR_BTN_X && tx < CLR_BTN_X+SCI_BTN_W)
				G_Init();
			else if (tx >= SCI_BTN_X && tx < SCI_BTN_X+SCI_BTN_W)
				memory->sciPage = (uint8_t)!memory->sciPage;
			else if (tx >= DEG_BTN_X && tx < DEG_BTN_X+SCI_BTN_W)
				memory->degrees = (uint8_t)!memory->degrees;
		}
    }
}

static void G_Draw(void)
{
	const FontDescription * f_big = &fontDescription[3];
	const FontDescription * f_med = &fontDescription[2];
	const FontDescription * f_small = &fontDescription[0];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,14,40), C_BLACK, GPU_X/2, 0, GPU_X/2, GPU_Y);
    const char nums[] = "123+456-789*.0/=";
    const char * sciLabels[16] = { "sqrt", "x^2", "x^y", "1/x",
                                   "sin", "cos", "tan", "+/-",
                                   "ln", "log", "pi", "e",
                                   ".", "%", "DEL", "BAS" };
    const char operation_c[8] = " +-*/=^";

    for (uint8_t i=0; i<4; i++)
    	for (uint8_t j=0; j<4; j++)
    	{
			uint8_t index = (uint8_t)(4*j+i);
			uint8_t isOperator;
			if (memory->sciPage)
				isOperator = (index == 2 || index == 15);
			else
				isOperator = (index == 3 || index == 7 || index == 11 || index == 14 || index == 15);
			GPU_Color key = isOperator ? C_C1 : C_WHITE;
			if (memory->_flashKey == index+1)
				key = isOperator ? C_C3 : C_GRAY_LIGHT;
			GPU_DrawBevelSquare(key, (int16_t)(i*BOX_W+2), (int16_t)(j*BOX_H+2),
			                    (int16_t)(BOX_W-4), (int16_t)(BOX_H-4));
			if (memory->sciPage)
			{
				const char * label = sciLabels[index];
				const FontDescription * f = (strlen(label) > 2) ? f_small : f_med;
				GPU_DrawTextCentered(isOperator ? C_WHITE : C_BLACK, f,
				                     (int16_t)(i*BOX_W+BOX_W/2), (int16_t)(j*BOX_H+BOX_H/2-f->height/2), label);
			}
			else
			{
				char label[2] = { nums[index], 0 };
				GPU_DrawTextCentered(isOperator ? C_WHITE : C_BLACK, f_big,
				                     (int16_t)(i*BOX_W+BOX_W/2), (int16_t)(j*BOX_H+BOX_H/2-f_big->height/2), label);
			}
    	}

    /* the display */
    GPU_DrawPanel(RGB(16,20,56), C_C1, (int16_t)(GPU_X/2+8), 8, (int16_t)(GPU_X/2-16), 110);
    char buffer[24];
    FormatNumber(buffer, memory->acc);
    GPU_DrawText(C_GRAY_MEDIUM, f_med, (int16_t)(GPU_X-16-(uint16_t)strlen(buffer)*f_med->width), 20, buffer, (uint16_t)strlen(buffer));
    GPU_DrawLetter(C_YELLOW, f_big, (int16_t)(GPU_X/2+16), 52, operation_c[(uint8_t)memory->operation]);
	if (memory->error)
		strcpy(buffer, "ERROR");
	else if (memory->inputActive)
		strcpy(buffer, memory->input);
	else
		FormatNumber(buffer, memory->acc);
    GPU_DrawText(C_WHITE, f_med, (int16_t)(GPU_X-16-(uint16_t)strlen(buffer)*f_med->width), 76, buffer, (uint16_t)strlen(buffer));
	/* mode line: keypad page and angle unit */
	{
		const char * mode = memory->sciPage ? "SCIENTIFIC" : "BASIC";
		GPU_DrawText(C_GRAY_DARK, f_small, (int16_t)(GPU_X/2+16), 100, mode, (uint16_t)strlen(mode));
	}

    /* mode buttons */
    GPU_DrawButton(C_RED, C_WHITE, f_small, "C", CLR_BTN_X, SCI_BTN_Y, SCI_BTN_W, SCI_BTN_H);
    GPU_DrawButton(memory->sciPage ? C_C3 : C_C1, C_BLACK, f_small,
                   memory->sciPage ? "BASIC" : "SCI", SCI_BTN_X, SCI_BTN_Y, SCI_BTN_W, SCI_BTN_H);
    GPU_DrawButton(memory->degrees ? C_C1 : C_C2, C_WHITE, f_small,
                   memory->degrees ? "DEG" : "RAD", DEG_BTN_X, SCI_BTN_Y, SCI_BTN_W, SCI_BTN_H);

    /* history tape of the last results */
    for (uint8_t i = 0; i < memory->_histN; i++)
    {
        strcpy(buffer, "= ");
        FormatNumber(buffer+2, memory->_hist[i]);
        GPU_DrawText(C_GRAY_MEDIUM, f_small, (int16_t)(GPU_X/2+16), (int16_t)(158 + i*12), buffer, (uint16_t)strlen(buffer));
    }
    GPU_DrawText(C_GRAY_DARK, f_small, (int16_t)(GPU_X/2+16), (int16_t)(GPU_Y-12), "tap the keys", 12);
}

static void G_Deinit(void)
{

}

const Callbacks G12_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };