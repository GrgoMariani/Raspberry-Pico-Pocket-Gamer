// SOLITAIRE - Klondike, draw one, with undo, auto-finish and a how-to-play card

#include "../mainLoop.h"
#include "../gpu.h"
#include "../interface.h"
#include "../gameshared.h"

#include <string.h>
#include <stdio.h>

/*
 * Layout: the top row holds the stock, the waste, the UNDO/AUTO/HELP buttons
 * and the four foundations; the seven tableau columns hang below it. Cards
 * are dealt from a shuffled 52 card deck, tableau builds down in alternating
 * colours, foundations build up per suit. Everything works with the
 * touchscreen (tap a card, tap the destination) and with the switches
 * (LEFT/RIGHT pick a pile, UP the card inside the pile, DOWN selects/places).
 *
 * To make the game readable for new players:
 *   - cards carry real suit symbols (hearts/diamonds red, spades/clubs black),
 *   - the stock shows how many cards are left and a recycle arrow when empty,
 *   - while a card is selected every legal destination is outlined in green,
 *   - a HOW TO PLAY overlay is shown on every deal until the first tap.
 */

#define SOL_CARD_W 40
#define SOL_CARD_H 50
#define SOL_SLOT(i) (4 + (i)*44)
#define SOL_TOP_Y 2
#define SOL_TAB_Y 58
#define SOL_TAB_MAX_H (232 - SOL_TAB_Y)

#define SOL_STOCK 0
#define SOL_WASTE 1
/* slot 2 = buttons */
#define SOL_FOUND0 3
#define SOL_TAB0 6

typedef struct { uint8_t card; uint8_t up; } SolCard;   /* card = suit*13 + rank, rank 0 = A */

typedef struct { SolCard c[52]; uint8_t n; } StockPile;
typedef struct { SolCard c[13]; uint8_t n; } FoundPile;
typedef struct { SolCard c[24]; uint8_t n; } TabPile;

typedef struct {
    StockPile stock, waste;
    FoundPile found[4];
    TabPile tab[7];
    uint16_t moves;
} SolBoard;

#define SOL_UNDO 8

typedef struct {
    SolBoard b;
    SolBoard undo[SOL_UNDO];
    uint8_t undoN;
    uint8_t selPile;        /* 255 = nothing selected, else 1 waste, 3..6 found, 6..12 tab */
    uint8_t selCard;        /* first card of the selected run */
    uint8_t cursor;         /* keyboard: 0 stock, 1 waste, 2..5 foundations, 6..12 tableau */
    uint8_t depth;          /* keyboard: how deep into the tableau pile */
    uint8_t won;
    uint8_t help;           /* the HOW TO PLAY overlay is on screen */
    uint8_t autoFlash;      /* ticks of the "AUTO" highlight */
    uint32_t startTime;
    uint32_t endTime;
} Game31Memory;

static Game31Memory * const memory = (Game31Memory*)(&mainMemory.sharedMemory.memblocks[0]);
static uint32_t bestTime = 0;

#define NUM_TICKS 20

static uint32_t CalculateTick()
{
    return IF_GetCurrentTime()/(ONE_SECOND/NUM_TICKS);
}

/* --------------------------------------------------------------- rules */

static uint8_t Suit(uint8_t card) { return (uint8_t)(card/13); }
static uint8_t Rank(uint8_t card) { return (uint8_t)(card%13); }
/* suit order: spades, hearts, diamonds, clubs */
static uint8_t IsRed(uint8_t card) { uint8_t s = Suit(card); return (uint8_t)(s == 1 || s == 2); }

static uint8_t ValidRun(const TabPile * p, uint8_t from)
{
    if (from >= p->n)
        return 0;
    for (uint8_t i = from; i < p->n; i++)
    {
        if (!p->c[i].up)
            return 0;
        if (i > from)
        {
            if (Rank(p->c[i-1].card) != Rank(p->c[i].card)+1)
                return 0;
            if (IsRed(p->c[i-1].card) == IsRed(p->c[i].card))
                return 0;
        }
    }
    return 1;
}

static uint8_t CanDropTab(uint8_t card, const TabPile * dst)
{
    if (dst->n == 0)
        return 1;                          /* any card may start an empty column */
    const SolCard * top = &dst->c[dst->n-1];
    if (!top->up)
        return 0;
    return (uint8_t)(Rank(top->card) == Rank(card)+1 && IsRed(top->card) != IsRed(card));
}

static uint8_t CanDropFound(uint8_t card, const FoundPile * dst)
{
    if (dst->n == 0)
        return (uint8_t)(Rank(card) == 0);           /* aces start a foundation */
    const SolCard * top = &dst->c[dst->n-1];
    return (uint8_t)(Suit(top->card) == Suit(card) && Rank(card) == Rank(top->card)+1);
}

/* --------------------------------------------------------------- state */

static void PushUndo(void)
{
    if (memory->undoN < SOL_UNDO)
    {
        memory->undo[memory->undoN] = memory->b;
        memory->undoN++;
    }
    else
    {
        for (uint8_t i = 1; i < SOL_UNDO; i++)
            memory->undo[i-1] = memory->undo[i];
        memory->undo[SOL_UNDO-1] = memory->b;
    }
}

static void Undo(void)
{
    if (!memory->undoN || memory->won)
        return;
    memory->undoN--;
    memory->b = memory->undo[memory->undoN];
    memory->selPile = 255;
}

static void CheckWin(void)
{
    uint8_t total = 0;
    for (uint8_t f = 0; f < 4; f++)
        total = (uint8_t)(total + memory->b.found[f].n);
    if (total == 52)
    {
        memory->won = 1;
        memory->endTime = IF_GetCurrentTime();
        if (bestTime == 0 || memory->endTime - memory->startTime < bestTime)
            bestTime = memory->endTime - memory->startTime;
        mainMemory._gameState = GS_DONE;
    }
}

/* flip the top card of a tableau pile when it got exposed */
static void FlipExposed(TabPile * p)
{
    if (p->n && !p->c[p->n-1].up)
        p->c[p->n-1].up = 1;
}

static void G_Init(void)
{
    memset(memory, 0, sizeof(Game31Memory));
    /* build and shuffle the deck */
    uint8_t deck[52];
    for (uint8_t i = 0; i < 52; i++)
        deck[i] = i;
    for (uint8_t i = 51; i > 0; i--)
    {
        uint8_t j = (uint8_t)Random_Range(i+1);
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    uint8_t next = 0;
    for (uint8_t col = 0; col < 7; col++)
    {
        memory->b.tab[col].n = (uint8_t)(col+1);
        for (uint8_t i = 0; i <= col; i++)
        {
            memory->b.tab[col].c[i].card = deck[next++];
            memory->b.tab[col].c[i].up = (uint8_t)(i == col);
        }
    }
    memory->b.stock.n = 0;
    while (next < 52)
    {
        memory->b.stock.c[memory->b.stock.n].card = deck[next++];
        memory->b.stock.c[memory->b.stock.n].up = 0;
        memory->b.stock.n++;
    }
    memory->selPile = 255;
    memory->help = 0;                 /* no forced hint screen; HELP button shows it on demand */
    memory->startTime = IF_GetCurrentTime();
    mainMemory._gameState = GS_RUNNING;
    mainMemory._lastRenderedTick = CalculateTick();
    Keyboard_GetPressedKeys();
    Touch_ClearTap();
}

static void DrawFromStock(void)
{
    PushUndo();
    memory->selPile = 255;
    if (memory->b.stock.n)
    {
        SolCard c = memory->b.stock.c[--memory->b.stock.n];
        c.up = 1;
        memory->b.waste.c[memory->b.waste.n++] = c;
    }
    else if (memory->b.waste.n)
    {
        /* recycle the waste back into the stock, keeping the order */
        while (memory->b.waste.n)
        {
            SolCard c = memory->b.waste.c[--memory->b.waste.n];
            c.up = 0;
            memory->b.stock.c[memory->b.stock.n++] = c;
        }
    }
    else
    {
        memory->undoN--;      /* nothing happened */
        return;
    }
    memory->b.moves++;
}

/* generic source description for a move */
typedef struct {
    uint8_t pile;       /* 1 waste, 3..6 foundation, 6..12 tableau (SOL_TAB0+) */
    uint8_t idx;
} SolSel;

static uint8_t SelCount(const SolSel * s)
{
    if (s->pile == SOL_WASTE) return 1;
    if (s->pile >= SOL_FOUND0 && s->pile < SOL_TAB0) return 1;
    return (uint8_t)(memory->b.tab[s->pile - SOL_TAB0].n - s->idx);
}

static uint8_t SelCard(const SolSel * s)
{
    if (s->pile == SOL_WASTE) return memory->b.waste.c[s->idx].card;
    if (s->pile >= SOL_FOUND0 && s->pile < SOL_TAB0)
        return memory->b.found[s->pile - SOL_FOUND0].c[s->idx].card;
    return memory->b.tab[s->pile - SOL_TAB0].c[s->idx].card;
}

/* remove the selection from its source and append it to a tableau pile */
static void MoveToTab(const SolSel * s, uint8_t col)
{
    PushUndo();
    TabPile * dst = &memory->b.tab[col];
    if (s->pile == SOL_WASTE)
    {
        dst->c[dst->n++] = memory->b.waste.c[--memory->b.waste.n];
    }
    else if (s->pile >= SOL_FOUND0 && s->pile < SOL_TAB0)
    {
        FoundPile * src = &memory->b.found[s->pile - SOL_FOUND0];
        dst->c[dst->n++] = src->c[--src->n];
    }
    else
    {
        TabPile * src = &memory->b.tab[s->pile - SOL_TAB0];
        for (uint8_t i = s->idx; i < src->n; i++)
            dst->c[dst->n++] = src->c[i];
        src->n = s->idx;
        FlipExposed(src);
    }
    memory->b.moves++;
    memory->selPile = 255;
}

static void MoveToFound(const SolSel * s, uint8_t f)
{
    PushUndo();
    FoundPile * dst = &memory->b.found[f];
    SolCard c;
    if (s->pile == SOL_WASTE)
    {
        c = memory->b.waste.c[--memory->b.waste.n];
    }
    else
    {
        TabPile * src = &memory->b.tab[s->pile - SOL_TAB0];
        c = src->c[--src->n];
        FlipExposed(src);
    }
    c.up = 1;
    dst->c[dst->n++] = c;
    memory->b.moves++;
    memory->selPile = 255;
    CheckWin();
}

/* try to send one card to any foundation; used by tap-again and AUTO */
static uint8_t TryFoundation(const SolSel * s)
{
    if (SelCount(s) != 1)
        return 0;
    if (s->pile >= SOL_FOUND0 && s->pile < SOL_TAB0)
        return 0;                       /* already on a foundation */
    uint8_t card = SelCard(s);
    for (uint8_t f = 0; f < 4; f++)
        if (CanDropFound(card, &memory->b.found[f]))
        {
            MoveToFound(s, f);
            return 1;
        }
    return 0;
}

/* AUTO: sweep every exposed card into the foundations until nothing moves */
static void AutoFinish(void)
{
    if (memory->won)
        return;
    uint8_t moved = 0;
    uint8_t guard = 0;
    PushUndo();
    for (;;)
    {
        uint8_t progress = 0;
        SolSel s;
        if (memory->b.waste.n)
        {
            s.pile = SOL_WASTE;
            s.idx = (uint8_t)(memory->b.waste.n-1);
            for (uint8_t f = 0; f < 4; f++)
                if (CanDropFound(SelCard(&s), &memory->b.found[f]))
                {
                    /* inline move without extra undo snapshots */
                    FoundPile * dst = &memory->b.found[f];
                    SolCard c = memory->b.waste.c[--memory->b.waste.n];
                    dst->c[dst->n++] = c;
                    memory->b.moves++;
                    progress = 1;
                    break;
                }
        }
        for (uint8_t col = 0; col < 7 && !progress; col++)
        {
            TabPile * p = &memory->b.tab[col];
            if (!p->n || !p->c[p->n-1].up)
                continue;
            for (uint8_t f = 0; f < 4; f++)
                if (CanDropFound(p->c[p->n-1].card, &memory->b.found[f]))
                {
                    SolCard c = p->c[--p->n];
                    memory->b.found[f].c[memory->b.found[f].n++] = c;
                    FlipExposed(p);
                    memory->b.moves++;
                    progress = 1;
                    break;
                }
        }
        if (!progress || ++guard > 60)
            break;
        moved = 1;
    }
    if (moved)
    {
        memory->autoFlash = 10;
        memory->selPile = 255;
        CheckWin();
    }
    else
    {
        memory->undoN--;      /* nothing happened */
    }
}

/* --------------------------------------------------------------- layout */

/* y position of every card of a tableau pile; long piles are compressed so
   they never run over the HUD line at the bottom */
static void TabLayout(uint8_t col, int16_t * ys)
{
    const TabPile * p = &memory->b.tab[col];
    uint8_t nd = 0;
    for (uint8_t i = 0; i < p->n; i++)
        if (!p->c[i].up) nd++;
    uint8_t nu = (uint8_t)(p->n - nd);
    int16_t downOff = 6, upOff = 14;
    if (p->n)
    {
        int16_t total = (int16_t)(nd*downOff + (nu ? (nu-1)*upOff : 0) + SOL_CARD_H);
        if (total > SOL_TAB_MAX_H && nu > 1)
        {
            upOff = (int16_t)((SOL_TAB_MAX_H - SOL_CARD_H - nd*downOff)/(nu-1));
            if (upOff < 7) upOff = 7;
            total = (int16_t)(nd*downOff + (nu-1)*upOff + SOL_CARD_H);
            if (total > SOL_TAB_MAX_H)
            {
                downOff = 3;
                if (nu > 1)
                {
                    upOff = (int16_t)((SOL_TAB_MAX_H - SOL_CARD_H - nd*downOff)/(nu-1));
                    if (upOff < 5) upOff = 5;
                }
            }
        }
    }
    int16_t y = SOL_TAB_Y;
    for (uint8_t i = 0; i < p->n; i++)
    {
        ys[i] = y;
        y = (int16_t)(y + (p->c[i].up ? upOff : downOff));
    }
}

/* --------------------------------------------------------------- input */

static void Select(uint8_t pile, uint8_t idx)
{
    memory->selPile = pile;
    memory->selCard = idx;
}

/* a tap (or the DOWN key) on a pile/card */
static void ActOn(uint8_t pile, uint8_t idx)
{
    if (memory->won)
        return;
    if (pile == SOL_STOCK)
    {
        DrawFromStock();
        return;
    }
    /* something selected: try to place it first */
    if (memory->selPile != 255)
    {
        SolSel s = { memory->selPile, memory->selCard };
        if (pile >= SOL_TAB0)
        {
            uint8_t col = (uint8_t)(pile - SOL_TAB0);
            if (memory->selPile == pile && memory->selCard == idx)
            {
                /* tapping the selection again: send it home if possible */
                if (!TryFoundation(&s))
                    memory->selPile = 255;
                return;
            }
            if (CanDropTab(SelCard(&s), &memory->b.tab[col]))
            {
                MoveToTab(&s, col);
                return;
            }
        }
        else if (pile >= SOL_FOUND0 && pile < SOL_TAB0)
        {
            uint8_t f = (uint8_t)(pile - SOL_FOUND0);
            if (SelCount(&s) == 1 && s.pile != pile &&
                (s.pile == SOL_WASTE || s.pile >= SOL_TAB0) &&
                CanDropFound(SelCard(&s), &memory->b.found[f]))
            {
                MoveToFound(&s, f);
                return;
            }
        }
        else if (pile == SOL_WASTE && memory->selPile == SOL_WASTE)
        {
            if (!TryFoundation(&s))
                memory->selPile = 255;
            return;
        }
        memory->selPile = 255;
        /* fall through: the tap may select whatever it hit instead */
    }
    /* select a card */
    if (pile == SOL_WASTE)
    {
        if (memory->b.waste.n)
            Select(SOL_WASTE, (uint8_t)(memory->b.waste.n-1));
        return;
    }
    if (pile >= SOL_FOUND0 && pile < SOL_TAB0)
    {
        FoundPile * p = &memory->b.found[pile - SOL_FOUND0];
        if (p->n)
            Select(pile, (uint8_t)(p->n-1));
        return;
    }
    if (pile >= SOL_TAB0)
    {
        TabPile * p = &memory->b.tab[pile - SOL_TAB0];
        if (!p->n)
            return;
        if (idx >= p->n)
            idx = (uint8_t)(p->n-1);
        if (!p->c[idx].up)
        {
            /* only the exposed face down card can be flipped */
            if (idx == p->n-1)
            {
                PushUndo();
                p->c[idx].up = 1;
                memory->b.moves++;
            }
            return;
        }
        if (ValidRun(p, idx))
            Select(pile, idx);
    }
}

static void HandleTap(uint16_t tx, uint16_t ty)
{
    if (memory->won)
    {
        G_Init();
        return;
    }
    if (ty < SOL_TAB_Y-2)
    {
        int slot = ((int)tx - 4)/44;
        if (slot < 0 || slot > 6)
            return;
        if (slot == 2)
        {
            /* UNDO / AUTO / HELP stacked in the button slot */
            if (ty < SOL_TOP_Y+17)
                Undo();
            else if (ty < SOL_TOP_Y+34)
                AutoFinish();
            else
                memory->help = 1;
            return;
        }
        if (slot == SOL_STOCK) { ActOn(SOL_STOCK, 0); return; }
        if (slot == SOL_WASTE) { ActOn(SOL_WASTE, 0); return; }
        ActOn((uint8_t)slot, 0);
        return;
    }
    int col = ((int)tx - 4)/44;
    if (col < 0 || col > 6)
        return;
    const TabPile * p = &memory->b.tab[col];
    uint8_t idx = 0;
    if (p->n)
    {
        int16_t ys[24];
        TabLayout((uint8_t)col, ys);
        idx = (uint8_t)(p->n-1);
        for (uint8_t i = 0; i < p->n; i++)
        {
            int16_t bottom = (i+1 < p->n) ? ys[i+1] : (int16_t)(ys[i]+SOL_CARD_H);
            if ((int16_t)ty >= ys[i] && (int16_t)ty < bottom)
            {
                idx = i;
                break;
            }
        }
    }
    ActOn((uint8_t)(SOL_TAB0 + col), idx);
}

static void Tick(void)
{
    if (memory->autoFlash)
        memory->autoFlash--;
    /* the how-to-play overlay swallow the first input and then disappears */
    if (memory->help)
    {
        uint16_t tx, ty;
        /* take the tap FIRST: short-circuiting on touchPressedNew used to leave
           it in the latch, so the dismissing tap also made a board move */
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            memory->help = 0;
        return;
    }
    if (memory->won)
    {
        uint16_t tx, ty;
        uint8_t tap = Touch_TakeTap(&tx, &ty);
        if (mainMemory.touchPressedNew || tap || Keyboard_GetPressedKeys() != KEY_NONE)
            G_Init();
        return;
    }
    KeyPressedEnum keys = Keyboard_GetPressedKeys();
    if (keys & KEY_LEFT)
    {
        memory->cursor = (uint8_t)((memory->cursor + 12) % 13);
        memory->depth = 0;
    }
    if (keys & KEY_RIGHT)
    {
        memory->cursor = (uint8_t)((memory->cursor + 1) % 13);
        memory->depth = 0;
    }
    if (keys & KEY_UP)
    {
        /* walk deeper into the selected tableau pile (towards its base) */
        if (memory->cursor >= 6)
        {
            const TabPile * p = &memory->b.tab[memory->cursor - 6];
            uint8_t up = 0;
            for (uint8_t i = 0; i < p->n; i++)
                if (p->c[i].up) up++;
            if (up)
                memory->depth = (uint8_t)((memory->depth + 1) % up);
        }
    }
    if (keys & KEY_DOWN)
    {
        uint8_t pile = memory->cursor;
        if (pile >= 2 && pile <= 5)
            pile = (uint8_t)(SOL_FOUND0 + (pile - 2));
        else if (pile >= 6)
            pile = (uint8_t)(SOL_TAB0 + (pile - 6));
        uint8_t idx = 0;
        if (pile >= SOL_TAB0)
        {
            const TabPile * p = &memory->b.tab[pile - SOL_TAB0];
            idx = p->n ? (uint8_t)(p->n-1 - memory->depth) : 0;
            if (p->n && memory->depth >= p->n)
                idx = 0;
        }
        ActOn(pile, idx);
    }
    uint16_t tx, ty;
    if (Touch_TakeTap(&tx, &ty))
        HandleTap(tx, ty);
}

static void G_Update(void)
{
    uint32_t tick = CalculateTick();
    if (tick > mainMemory._lastRenderedTick + 2*NUM_TICKS)
        mainMemory._lastRenderedTick = tick - 2*NUM_TICKS;
    while (mainMemory._lastRenderedTick < tick)
    {
        mainMemory._lastRenderedTick++;
        Tick();
    }
}

/* --------------------------------------------------------------- drawing */

static const char * _rankStr[13] = { "A","2","3","4","5","6","7","8","9","10","J","Q","K" };

/* 8x8 suit symbols, MSB = leftmost pixel (the GPU_DrawSprite convention),
   stored pre-shifted into the high byte of each row */
static const uint16_t _suitSpade[8]   = { 0x1800,0x3C00,0x7E00,0xFF00,0xFF00,0x7E00,0x1800,0x3C00 };
static const uint16_t _suitHeart[8]   = { 0x6600,0xFF00,0xFF00,0xFF00,0x7E00,0x3C00,0x1800,0x0000 };
static const uint16_t _suitDiamond[8] = { 0x1800,0x3C00,0x7E00,0xFF00,0xFF00,0x7E00,0x3C00,0x1800 };
static const uint16_t _suitClub[8]    = { 0x1800,0x3C00,0x1800,0x6600,0xFF00,0xFF00,0x1800,0x3C00 };
static const uint16_t * const _suitSprite[4] = { _suitSpade, _suitHeart, _suitDiamond, _suitClub };

static void DrawSuit(uint8_t suit, int16_t x, int16_t y, GPU_Color color, uint8_t scale)
{
    GPU_DrawSprite(_suitSprite[suit], 8, 8, x, y, color, scale);
}

static void DrawCardFace(int16_t x, int16_t y, uint8_t card, uint8_t full, uint8_t selected)
{
    int16_t h = full ? SOL_CARD_H : 14;
    GPU_Color border = selected ? C_YELLOW : C_GRAY_DARK;
    GPU_DrawFilledSquare(C_WHITE, x, y, SOL_CARD_W, h);
    GPU_DrawEmptySquare(border, x, y, SOL_CARD_W, h);
    if (selected)
        GPU_DrawEmptySquare(C_YELLOW, x+1, y+1, SOL_CARD_W-2, h-2);
    GPU_Color sc = IsRed(card) ? C_RED : C_BLACK;
    const FontDescription * fs = &fontDescription[0];
    const char * rank = _rankStr[Rank(card)];
    uint8_t rw = (uint8_t)strlen(rank);
    GPU_DrawText(sc, fs, x+3, y+3, rank, rw);
    /* small suit right next to the rank so the colour alone is not the cue */
    DrawSuit(Suit(card), (int16_t)(x+3+rw*6+1), y+3, sc, 1);
    if (full)
    {
        /* big suit in the middle of the card */
        DrawSuit(Suit(card), (int16_t)(x+SOL_CARD_W/2-8), (int16_t)(y+26), sc, 2);
    }
}

static void DrawCardBack(int16_t x, int16_t y, int16_t h)
{
    GPU_DrawFilledSquare(RGB(40,50,130), x, y, SOL_CARD_W, h);
    GPU_DrawEmptySquare(RGB(90,100,190), x, y, SOL_CARD_W, h);
    if (h >= SOL_CARD_H)
    {
        GPU_DrawEmptySquare(RGB(70,80,170), x+4, y+4, SOL_CARD_W-8, h-8);
        /* a little diamond motif so the back reads as a playing card */
        int16_t cx = (int16_t)(x+SOL_CARD_W/2), cy = (int16_t)(y+h/2);
        GPU_DrawFilledTriangle(RGB(90,100,190), cx, (int16_t)(cy-6), (int16_t)(cx-5), cy, (int16_t)(cx+5), cy);
        GPU_DrawFilledTriangle(RGB(90,100,190), (int16_t)(cx-5), cy, (int16_t)(cx+5), cy, cx, (int16_t)(cy+6));
    }
}

static void DrawEmptySlot(int16_t x, int16_t y, const char * label)
{
    GPU_DrawEmptySquare(RGB(30,80,40), x, y, SOL_CARD_W, SOL_CARD_H);
    if (label)
        GPU_DrawTextCentered(RGB(60,120,70), &fontDescription[2], (int16_t)(x+SOL_CARD_W/2),
                             (int16_t)(y+SOL_CARD_H/2-9), label);
}

/* the recycle arrow shown on the empty stock */
static void DrawRecycle(int16_t x, int16_t y)
{
    GPU_DrawEmptySquare(RGB(30,80,40), x, y, SOL_CARD_W, SOL_CARD_H);
    int16_t cx = (int16_t)(x+SOL_CARD_W/2), cy = (int16_t)(y+SOL_CARD_H/2);
    GPU_Color c = RGB(70,140,80);
    GPU_DrawEmptyCircle(c, cx, cy, 9);
    /* arrow head on the top right of the ring */
    GPU_DrawFilledTriangle(c, (int16_t)(cx+6), (int16_t)(cy-12), (int16_t)(cx+13), (int16_t)(cy-6), (int16_t)(cx+4), (int16_t)(cy-4));
}

/* green outline around every pile the current selection can be dropped on */
static void DrawDropHints(void)
{
    if (memory->selPile == 255)
        return;
    SolSel s = { memory->selPile, memory->selCard };
    uint8_t card = SelCard(&s);
    GPU_Color c = C_C3;
    for (uint8_t col = 0; col < 7; col++)
    {
        if (memory->selPile == SOL_TAB0+col)
            continue;                       /* not onto itself */
        if (!CanDropTab(card, &memory->b.tab[col]))
            continue;
        int16_t x = SOL_SLOT(col);
        const TabPile * p = &memory->b.tab[col];
        int16_t y = SOL_TAB_Y;
        if (p->n)
        {
            int16_t ys[24];
            TabLayout(col, ys);
            y = ys[p->n-1];
        }
        GPU_DrawEmptySquare(c, (int16_t)(x-1), (int16_t)(y-1), SOL_CARD_W+2, SOL_CARD_H+2);
    }
    if (SelCount(&s) == 1 && (s.pile == SOL_WASTE || s.pile >= SOL_TAB0))
    {
        for (uint8_t f = 0; f < 4; f++)
        {
            if (memory->selPile == SOL_FOUND0+f)
                continue;
            if (CanDropFound(card, &memory->b.found[f]))
                GPU_DrawEmptySquare(c, (int16_t)(SOL_SLOT(SOL_FOUND0+f)-1), (int16_t)(SOL_TOP_Y-1),
                                    SOL_CARD_W+2, SOL_CARD_H+2);
        }
    }
}

static void DrawHelp(void)
{
    GPU_DrawPanel(RGB(12,30,16), C_C3, 10, 18, 300, 204);
    const FontDescription * ft = &fontDescription[1];
    const FontDescription * fs = &fontDescription[0];
    GPU_DrawTextOutlined(C_C3, C_BLACK, ft, GPU_X/2, 26, "HOW TO PLAY SOLITAIRE");
    static const char * lines[] = {
        "Tap the deck (top left) to draw a card.",
        "Tap a card, then tap where to move it.",
        "Green outlines show legal destinations.",
        "",
        "Tableau: build DOWN in alternating",
        "colours. Any card on an empty column.",
        "Foundations: build UP by suit, ACE first.",
        "",
        "Tap a selected card again to send it to",
        "a foundation when it fits. UNDO takes a",
        "move back, AUTO finishes the game.",
        "",
        "Switches: L/R pile, UP card, DOWN move.",
    };
    int16_t y = 44;
    for (unsigned i = 0; i < sizeof(lines)/sizeof(lines[0]); i++)
    {
        GPU_DrawText(C_WHITE, fs, 20, y, lines[i], (uint16_t)strlen(lines[i]));
        y = (int16_t)(y + 11);
    }
    GPU_DrawTextCentered(C_YELLOW, fs, GPU_X/2, 202, "- tap or press any key to play -");
}

static void G_Draw(void)
{
    const FontDescription * fs = &fontDescription[0];
    const FontDescription * fm = &fontDescription[1];
    char text[48];
    GPU_ClearFramebuffers();
    GPU_DrawVerticalGradient(RGB(10,60,28), RGB(4,28,12), 0, 0, GPU_X, GPU_Y);

    /* stock: a face down deck with the number of cards left on it */
    if (memory->b.stock.n)
    {
        DrawCardBack(SOL_SLOT(0), SOL_TOP_Y, SOL_CARD_H);
        sprintf(text, "%u", (unsigned)memory->b.stock.n);
        GPU_DrawText(C_WHITE, fs, (int16_t)(SOL_SLOT(0)+3), (int16_t)(SOL_TOP_Y+SOL_CARD_H-11),
                     text, (uint16_t)strlen(text));
    }
    else
        DrawRecycle(SOL_SLOT(0), SOL_TOP_Y);
    /* waste */
    if (memory->b.waste.n)
    {
        uint8_t top = (uint8_t)(memory->b.waste.n-1);
        DrawCardFace(SOL_SLOT(1), SOL_TOP_Y, memory->b.waste.c[top].card, 1,
                     (uint8_t)(memory->selPile == SOL_WASTE));
    }
    else
        DrawEmptySlot(SOL_SLOT(1), SOL_TOP_Y, 0);
    /* buttons: UNDO / AUTO / HELP */
    GPU_DrawButton(memory->undoN ? C_C1 : C_GRAY_DARK, C_WHITE, fs, "UNDO",
                   SOL_SLOT(2), SOL_TOP_Y, SOL_CARD_W, 15);
    GPU_DrawButton(memory->autoFlash ? C_C3 : C_C2, C_WHITE, fs, "AUTO",
                   SOL_SLOT(2), SOL_TOP_Y+17, SOL_CARD_W, 15);
    GPU_DrawButton(C_GRAY_MEDIUM, C_BLACK, fs, "HELP",
                   SOL_SLOT(2), SOL_TOP_Y+34, SOL_CARD_W, 14);
    /* foundations */
    for (uint8_t f = 0; f < 4; f++)
    {
        int16_t x = SOL_SLOT(SOL_FOUND0+f);
        const FoundPile * p = &memory->b.found[f];
        if (p->n)
            DrawCardFace(x, SOL_TOP_Y, p->c[p->n-1].card, 1,
                         (uint8_t)(memory->selPile == SOL_FOUND0+f));
        else
            DrawEmptySlot(x, SOL_TOP_Y, "A");
    }
    /* tableau */
    for (uint8_t col = 0; col < 7; col++)
    {
        int16_t x = SOL_SLOT(col);
        const TabPile * p = &memory->b.tab[col];
        if (!p->n)
        {
            DrawEmptySlot(x, SOL_TAB_Y, 0);
            continue;
        }
        int16_t ys[24];
        TabLayout(col, ys);
        for (uint8_t i = 0; i < p->n; i++)
        {
            uint8_t full = (uint8_t)(i == p->n-1);
            uint8_t selected = (uint8_t)(memory->selPile == SOL_TAB0+col && i >= memory->selCard);
            if (p->c[i].up)
                DrawCardFace(x, ys[i], p->c[i].card, full, selected);
            else
                DrawCardBack(x, ys[i], full ? SOL_CARD_H : 6);
        }
    }
    /* legal destinations for the current selection */
    DrawDropHints();
    /* keyboard cursor */
    if (!memory->won && !memory->help)
    {
        int16_t cx = 0, cy = 0, ch = SOL_CARD_H;
        uint8_t pile = memory->cursor;
        if (pile == 0) { cx = SOL_SLOT(0); cy = SOL_TOP_Y; }
        else if (pile == 1) { cx = SOL_SLOT(1); cy = SOL_TOP_Y; }
        else if (pile <= 5) { cx = SOL_SLOT(SOL_FOUND0 + pile-2); cy = SOL_TOP_Y; }
        else
        {
            uint8_t col = (uint8_t)(pile-6);
            const TabPile * p = &memory->b.tab[col];
            cx = SOL_SLOT(col);
            cy = SOL_TAB_Y;
            if (p->n)
            {
                int16_t ys[24];
                TabLayout(col, ys);
                uint8_t idx = (uint8_t)(p->n-1 - memory->depth);
                if (memory->depth >= p->n) idx = 0;
                cy = ys[idx];
                ch = (idx == p->n-1) ? SOL_CARD_H : (int16_t)(ys[idx+1]-ys[idx]);
                if (ch < 4) ch = 4;
            }
        }
        GPU_DrawEmptySquare(C_GRAY_LIGHT, (int16_t)(cx-1), (int16_t)(cy-1), SOL_CARD_W+2, ch+2);
    }
    /* HUD line */
    {
        uint32_t now = memory->won ? memory->endTime : IF_GetCurrentTime();
        int len = sprintf(text, "MOVES %u  TIME %us", (unsigned)memory->b.moves,
                          (unsigned)((now - memory->startTime)/ONE_SECOND));
        if (bestTime)
            len += sprintf(text+len, "  BEST %us", (unsigned)(bestTime/ONE_SECOND));
        GPU_DrawText(C_GRAY_LIGHT, fs, 4, (int16_t)(GPU_Y-9), text, (uint16_t)len);
    }
    if (memory->won)
    {
        GPU_DrawPanel(RGB(16,40,20), C_C3, (int16_t)(GPU_X/2-92), (int16_t)(GPU_Y/2-40), 184, 84);
        GPU_DrawTextOutlined(C_C3, C_BLACK, &fontDescription[2], GPU_X/2, (int16_t)(GPU_Y/2-32), "YOU WIN!");
        sprintf(text, "%u moves  %u s", (unsigned)memory->b.moves,
                (unsigned)((memory->endTime - memory->startTime)/ONE_SECOND));
        GPU_DrawTextCentered(C_WHITE, fm, GPU_X/2, (int16_t)(GPU_Y/2-4), text);
        GPU_DrawTextCentered(C_GRAY_MEDIUM, fm, GPU_X/2, (int16_t)(GPU_Y/2+16), "tap or press a key");
    }
    if (memory->help)
        DrawHelp();
}

static void G_Deinit(void)
{
}

const Callbacks G31_Callbacks = { &G_Init, &G_Update, &G_Draw, &G_Deinit };