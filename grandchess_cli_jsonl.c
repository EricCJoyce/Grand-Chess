#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gamestate.h"
#include "ambrosia.h"
#include "jsmn.h"

#define ENCODE_OFFSET_WHITE_PAWNS     1                             /* Bytes [1..10]. */
#define ENCODE_OFFSET_BLACK_PAWNS     11                            /* Bytes [11..20]. */
#define ENCODE_OFFSET_WHITE_KNIGHTS   21                            /* Bytes [21..22]. */
#define ENCODE_OFFSET_BLACK_KNIGHTS   23                            /* Bytes [23..24]. */
#define ENCODE_OFFSET_WHITE_BISHOPS   25                            /* Bytes [25..26]. */
#define ENCODE_OFFSET_BLACK_BISHOPS   27                            /* Bytes [27..28]. */
#define ENCODE_OFFSET_WHITE_ROOKS     29                            /* Bytes [29..30]. */
#define ENCODE_OFFSET_BLACK_ROOKS     31                            /* Bytes [31..32]. */
#define ENCODE_OFFSET_WHITE_CARDINAL  33                            /* Byte [33]. */
#define ENCODE_OFFSET_BLACK_CARDINAL  34                            /* Byte [34]. */
#define ENCODE_OFFSET_WHITE_MARSHAL   35                            /* Byte [35]. */
#define ENCODE_OFFSET_BLACK_MARSHAL   36                            /* Byte [36]. */
#define ENCODE_OFFSET_WHITE_QUEEN     37                            /* Byte [37]. */
#define ENCODE_OFFSET_BLACK_QUEEN     38                            /* Byte [38]. */
#define ENCODE_OFFSET_WHITE_KING      39                            /* Byte [39]. */
#define ENCODE_OFFSET_BLACK_KING      40                            /* Byte [40]. */
#define ENCODE_OFFSET_MOVE_CTR        41                            /* Byte [41]. */

static int tok_eq(const char*, const jsmntok_t*, const char*);
static int tok_copy_string(const char*, const jsmntok_t*, char*, size_t);
static int json_find_top_value(const char*, const jsmntok_t*, int, const char*);
static int hex_nibble(char);
static int hex_decode_42(const char*, uint8_t[_GAMESTATE_BYTE_SIZE]);
static void hex_encode(const uint8_t*, size_t, char*);
static int move_hex_decode_3(const char*, Move*);
static void move_hex_encode_3(Move*, char[7]);
void serialize(GameState*, unsigned char*);
void deserialize(const uint8_t*, GameState*);

//  echo '{"cmd":"startpos"}' | ./grandchess_cli
//  echo '{"cmd":"draw","state_hex":"801415161718191a1b1c1d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli
//  echo '{"cmd":"features","state_hex":"801415161718191a1b1c1d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli
//  echo '{"cmd":"phase_alphas","state_hex":"801415161718191a1b1c1d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli

//  echo '{"cmd":"legal_moves","state_hex":"801415161718191a1b1c1d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli
//  echo '{"cmd":"apply_move","state_hex":"801415161718191a1b1c1d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400","move_hex":"192d00"}' | ./grandchess_cli

//  echo '{"cmd":"draw","state_hex":"0614151617181a1b1c1d2d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli
//  echo '{"cmd":"features","state_hex":"0614151617181a1b1c1d2d464748494a4b4c4d4e4f0b1251580c11525700095a6310560f550d530e5400"}' | ./grandchess_cli

static int handle_request(const char* line);

int main(void)
  {
    char line[4096];

    while(fgets(line, sizeof(line), stdin))
      {
        int rc = handle_request(line);
        fflush(stdout);
        fflush(stderr);
        if(rc != 0)
          return rc;
      }

    return 0;
  }

static int handle_request(const char* line)
  {
    unsigned int i;
    int ntok;
    jsmn_parser p;                                                  //  Parse JSON.
    jsmntok_t toks[1024];
    int cmd_i;
    char cmd[128];
    uint8_t state[_GAMESTATE_BYTE_SIZE];
    uint8_t next_state[_GAMESTATE_BYTE_SIZE];
    char hex[2 * _GAMESTATE_BYTE_SIZE + 1];
    char state_hex[2 * _GAMESTATE_BYTE_SIZE + 1];
    int st_i;
    GameState gs;

    unsigned char whiteMaterial[20];                                //  Indices of all white material.
    unsigned char whiteMaterialLength;
    unsigned char blackMaterial[20];                                //  Indices of all black material.
    unsigned char blackMaterialLength;

    Move whiteMoves[_MAX_MOVES];
    unsigned int whiteMovesLength;
    Move blackMoves[_MAX_MOVES];
    unsigned int blackMovesLength;

    Move whitePawnAttacks[_MAX_MOVES];                              //  Actual pawn attacks only, not pawn-attackable squares.
    unsigned int whitePawnAttacksLength;
    Move blackPawnAttacks[_MAX_MOVES];
    unsigned int blackPawnAttacksLength;

    Move whitePawnTargets[_MAX_MOVES];                              //  Pawn-attackable squares, not necessarily actual attacks.
    unsigned int whitePawnTargetsLength;
    Move blackPawnTargets[_MAX_MOVES];
    unsigned int blackPawnTargetsLength;

    Move whiteCoverage[_MAX_MOVES];                                 //  Ally-occupied squares that are theoretically attackable.
    unsigned int whiteCoverageLength;
    Move blackCoverage[_MAX_MOVES];
    unsigned int blackCoverageLength;

    Move whitePawnCoverage[_MAX_MOVES];                             //  Coverage, but by pawns only.
    unsigned int whitePawnCoverageLength;
    Move blackPawnCoverage[_MAX_MOVES];
    unsigned int blackPawnCoverageLength;

    Move whiteScope[_MAX_MOVES];                                    //  Scope is all squares theoretically reachable: empty, ally-occupied, enemy-occupied.
    unsigned int whiteScopeLength;
    Move blackScope[_MAX_MOVES];
    unsigned int blackScopeLength;

    Move whiteXRay[_MAX_MOVES];                                     //  X-Ray attacks are enemy-occupied squares if we could pass through allies (and empties).
    unsigned int whiteXRayLength;
    Move blackXRay[_MAX_MOVES];
    unsigned int blackXRayLength;

    float f[11];

    int is_term;
    int res;

    Move moves[_MAX_MOVES];
    unsigned int movesLen = 0;
    char mh[7];
    int mv_i;

    char move_hex[7];
    Move mv;
    float phaseWeights[3];

    //////////////////////////////////////////////////////////////////
    jsmn_init(&p);
    ntok = jsmn_parse(&p, line, (int)strlen(line), toks, (int)(sizeof(toks)/sizeof(toks[0])));
    if(ntok < 0)
      {
        fprintf(stderr, "JSON parse error: %d\n", ntok);
        return 3;
      }

    cmd_i = json_find_top_value(line, toks, ntok, "cmd");           //  cmd
    if(cmd_i < 0 || toks[cmd_i].type != JSMN_STRING)
      {
        fprintf(stderr, "Missing/invalid 'cmd'.\n");
        return 4;
      }

    if(!tok_copy_string(line, &toks[cmd_i], cmd, sizeof(cmd)))
      {
        fprintf(stderr, "'cmd' too long.\n");
        return 4;
      }

    if(strcmp(cmd, "startpos") == 0)
      {
                                                                    //  Write the start set.
        state[0]  = 128; state[1]  = 20; state[2]  = 21; state[3]  = 22; state[4]  = 23; state[5]  = 24; state[6]  = 25;
        state[7]  =  26; state[8]  = 27; state[9]  = 28; state[10] = 29; state[11] = 70; state[12] = 71; state[13] = 72;
        state[14] =  73; state[15] = 74; state[16] = 75; state[17] = 76; state[18] = 77; state[19] = 78; state[20] = 79;
        state[21] =  11; state[22] = 18; state[23] = 81; state[24] = 88; state[25] = 12; state[26] = 17; state[27] = 82;
        state[28] =  87; state[29] =  0; state[30] =  9; state[31] = 90; state[32] = 99; state[33] = 16; state[34] = 86;
        state[35] =  15; state[36] = 85; state[37] = 13; state[38] = 83; state[39] = 14; state[40] = 84; state[41] =  0;
        hex_encode(state, _GAMESTATE_BYTE_SIZE, hex);
        printf("{\"state_hex\":\"%s\"}\n", hex);
        return 0;
      }

    if(strcmp(cmd, "print_move") == 0)
      {
        mv_i = json_find_top_value(line, toks, ntok, "move_hex");
        if(mv_i < 0 || toks[mv_i].type != JSMN_STRING)
          {
            fprintf(stderr, "Missing/invalid 'move_hex'.\n");
            return 6;
          }

        if(!tok_copy_string(line, &toks[mv_i], move_hex, sizeof(move_hex)))
          {
            fprintf(stderr, "'move_hex' wrong length.\n");
            return 6;
          }

        if(!move_hex_decode_3(move_hex, &mv))
          {
            fprintf(stderr, "Bad hex in 'move_hex'.\n");
            return 6;
          }

        if(mv.promo == _NO_PROMO)
          printf("{\"move_from\":%d, \"move_to\":%d}\n", mv.from, mv.to);
        else
          {
            switch(mv.promo)
              {
                case _PROMO_KNIGHT:    printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"N\"}\n", mv.from, mv.to);  break;
                case _PROMO_BISHOP:    printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"B\"}\n", mv.from, mv.to);  break;
                case _PROMO_ROOK:      printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"R\"}\n", mv.from, mv.to);  break;
                case _PROMO_CARDINAL:  printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"C\"}\n", mv.from, mv.to);  break;
                case _PROMO_MARSHAL:   printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"M\"}\n", mv.from, mv.to);  break;
                case _PROMO_QUEEN:     printf("{\"move_from\":%d, \"move_to\":%d, \"promo\":\"Q\"}\n", mv.from, mv.to);  break;
              }
          }

        return 0;
      }

    st_i = json_find_top_value(line, toks, ntok, "state_hex");      //  All other commands require state_hex.
    if(st_i < 0 || toks[st_i].type != JSMN_STRING)
      {
        fprintf(stderr, "Missing/invalid 'state_hex'.\n");
        return 5;
      }

    if(!tok_copy_string(line, &toks[st_i], state_hex, sizeof(state_hex)))
      {
        fprintf(stderr, "'state_hex' wrong length.\n");
        return 5;
      }

    if(!hex_decode_42(state_hex, state))
      {
        fprintf(stderr, "Bad hex in 'state_hex'.\n");
        return 5;
      }

    memset(&gs, 0, sizeof(gs));
    deserialize(state, &gs);

    if(strcmp(cmd, "draw") == 0)
      {
        printf("{");
        printf("\"row_10\":\"");
        for(i = 90; i < _NONE; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_9\":\"");
        for(i = 80; i < 90; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_8\":\"");
        for(i = 70; i < 80; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_7\":\"");
        for(i = 60; i < 70; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_6\":\"");
        for(i = 50; i < 60; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_5\":\"");
        for(i = 40; i < 50; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_4\":\"");
        for(i = 30; i < 40; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_3\":\"");
        for(i = 20; i < 30; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_2\":\"");
        for(i = 10; i < 20; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        printf("\"row_1\":\"");
        for(i = 0; i < 10; i++)
          {
            switch(gs.board[i])
              {
                case _EMPTY:           printf(".");  break;
                case _WHITE_PAWN:      printf("P");  break;
                case _WHITE_KNIGHT:    printf("N");  break;
                case _WHITE_BISHOP:    printf("B");  break;
                case _WHITE_ROOK:      printf("R");  break;
                case _WHITE_CARDINAL:  printf("C");  break;
                case _WHITE_MARSHAL:   printf("M");  break;
                case _WHITE_QUEEN:     printf("Q");  break;
                case _WHITE_KING:      printf("K");  break;
                case _BLACK_PAWN:      printf("p");  break;
                case _BLACK_KNIGHT:    printf("n");  break;
                case _BLACK_BISHOP:    printf("b");  break;
                case _BLACK_ROOK:      printf("r");  break;
                case _BLACK_CARDINAL:  printf("c");  break;
                case _BLACK_MARSHAL:   printf("m");  break;
                case _BLACK_QUEEN:     printf("q");  break;
                case _BLACK_KING:      printf("k");  break;
              }
          }
        printf("\",");

        if(gs.whiteToMove)
          printf("\"white_to_move\":true}\n");
        else
          printf("\"white_to_move\":false}\n");

        return 0;
      }

    if(strcmp(cmd, "features") == 0)
      {
        //////////////////////////////////////////////////////////////  Compute the following only ONCE
        whiteMaterialLength = getWhite(&gs, whiteMaterial);         //  unsigned chars
        blackMaterialLength = getBlack(&gs, blackMaterial);         //  unsigned chars

        whiteMovesLength = getMovesForTeam(true, &gs, whiteMoves);  //  Moves
        blackMovesLength = getMovesForTeam(false, &gs, blackMoves); //  Moves
                                                                    //  Pawn Attack Moves
        whitePawnAttacksLength = getPawnAttacksTeam(true, &gs, whitePawnAttacks);
                                                                    //  Pawn Attack Moves
        blackPawnAttacksLength = getPawnAttacksTeam(false, &gs, blackPawnAttacks);
                                                                    //  Pawn Target Moves
        whitePawnTargetsLength = getPawnTargetsTeam(true, &gs, whitePawnTargets);
                                                                    //  Pawn Target Moves
        blackPawnTargetsLength = getPawnTargetsTeam(false, &gs, blackPawnTargets);

        whiteCoverageLength = getCoverage(true, &gs, whiteCoverage);//  Coverage Moves
        blackCoverageLength = getCoverage(false, &gs, blackCoverage);

        whitePawnCoverageLength = 0;                                //  Coverage only by pawns
        for(i = 0; i < whiteCoverageLength; i++)                    //  Count up pawn coverage.
          {
            if(isPawn(whiteCoverage[i].from, &gs))
              {
                whitePawnCoverage[whitePawnCoverageLength].from = whiteCoverage[i].from;
                whitePawnCoverage[whitePawnCoverageLength].to = whiteCoverage[i].to;
                whitePawnCoverage[whitePawnCoverageLength].promo = whiteCoverage[i].promo;
                whitePawnCoverageLength++;
              }
          }
        blackPawnCoverageLength = 0;
        for(i = 0; i < blackCoverageLength; i++)
          {
            if(isPawn(blackCoverage[i].from, &gs))
              {
                blackPawnCoverage[blackPawnCoverageLength].from = blackCoverage[i].from;
                blackPawnCoverage[blackPawnCoverageLength].to = blackCoverage[i].to;
                blackPawnCoverage[blackPawnCoverageLength].promo = blackCoverage[i].promo;
                blackPawnCoverageLength++;
              }
          }

        whiteScopeLength = getScope(true, &gs, whiteScope);         //  Scope
        blackScopeLength = getScope(false, &gs, blackScope);

        whiteXRayLength = getXRay(true, &gs, whiteXRay);            //  X-Ray attacks
        blackXRayLength = getXRay(false, &gs, blackXRay);

        if(gs.whiteToMove)
          {
            f[0] = material(whiteMaterial, whiteMaterialLength, &gs) - material(blackMaterial, blackMaterialLength, &gs);
            f[1] = mobility(whiteMoves, whiteMovesLength, &gs) - mobility(blackMoves, blackMovesLength, &gs);
            f[2] = attacks(whiteMoves, whiteMovesLength, blackMoves, blackMovesLength, &gs) - attacks(blackMoves, blackMovesLength, whiteMoves, whiteMovesLength, &gs);
            f[3] = coverage(whiteCoverage, whiteCoverageLength, &gs) - coverage(blackCoverage, blackCoverageLength, &gs);
            f[4] = pawnstructure(whiteMaterial, whiteMaterialLength, whitePawnCoverage, whitePawnCoverageLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, &gs) - pawnstructure(blackMaterial, blackMaterialLength, blackPawnCoverage, blackPawnCoverageLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, &gs);
            f[5] = development(true, &gs) - development(false, &gs);
            f[6] = pieceeval(whiteMaterial, whiteMaterialLength, whiteMoves, whiteMovesLength, whiteCoverage, whiteCoverageLength, whitePawnCoverage, whitePawnCoverageLength, whitePawnTargets, whitePawnTargetsLength, whiteScope, whiteScopeLength, whiteXRay, whiteXRayLength, blackMaterial, blackMaterialLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, &gs) - pieceeval(blackMaterial, blackMaterialLength, blackMoves, blackMovesLength, blackCoverage, blackCoverageLength, blackPawnCoverage, blackPawnCoverageLength, blackPawnTargets, blackPawnTargetsLength, blackScope, blackScopeLength, blackXRay, blackXRayLength, whiteMaterial, whiteMaterialLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, &gs);
            f[7] = centercontrol(true, whiteMoves, whiteMovesLength, whitePawnAttacks, whitePawnAttacksLength) - centercontrol(false, blackMoves, blackMovesLength, blackPawnAttacks, blackPawnAttacksLength);
            f[8] = vulnerability(whiteMoves, whiteMovesLength, &gs) - vulnerability(blackMoves, blackMovesLength, &gs);
            f[9] = trapped(whiteMoves, whiteMovesLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, blackCoverage, blackCoverageLength, &gs) - trapped(blackMoves, blackMovesLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, whiteCoverage, whiteCoverageLength, &gs);
            f[10] = pins(whiteMaterial, whiteMaterialLength, blackMoves, blackMovesLength, whiteCoverage, whiteCoverageLength, blackCoverage, blackCoverageLength, &gs) - pins(blackMaterial, blackMaterialLength, whiteMoves, whiteMovesLength, blackCoverage, blackCoverageLength, whiteCoverage, whiteCoverageLength, &gs);
          }
        else
          {
            f[0] = material(blackMaterial, blackMaterialLength, &gs) - material(whiteMaterial, whiteMaterialLength, &gs);
            f[1] = mobility(blackMoves, blackMovesLength, &gs) - mobility(whiteMoves, whiteMovesLength, &gs);
            f[2] = attacks(blackMoves, blackMovesLength, whiteMoves, whiteMovesLength, &gs) - attacks(whiteMoves, whiteMovesLength, blackMoves, blackMovesLength, &gs);
            f[3] = coverage(blackCoverage, blackCoverageLength, &gs) - coverage(whiteCoverage, whiteCoverageLength, &gs);
            f[4] = pawnstructure(blackMaterial, blackMaterialLength, blackPawnCoverage, blackPawnCoverageLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, &gs) - pawnstructure(whiteMaterial, whiteMaterialLength, whitePawnCoverage, whitePawnCoverageLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, &gs);
            f[5] = development(false, &gs) - development(true, &gs);
            f[6] = pieceeval(blackMaterial, blackMaterialLength, blackMoves, blackMovesLength, blackCoverage, blackCoverageLength, blackPawnCoverage, blackPawnCoverageLength, blackPawnTargets, blackPawnTargetsLength, blackScope, blackScopeLength, blackXRay, blackXRayLength, whiteMaterial, whiteMaterialLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, &gs) - pieceeval(whiteMaterial, whiteMaterialLength, whiteMoves, whiteMovesLength, whiteCoverage, whiteCoverageLength, whitePawnCoverage, whitePawnCoverageLength, whitePawnTargets, whitePawnTargetsLength, whiteScope, whiteScopeLength, whiteXRay, whiteXRayLength, blackMaterial, blackMaterialLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, &gs);
            f[7] = centercontrol(false, blackMoves, blackMovesLength, blackPawnAttacks, blackPawnAttacksLength) - centercontrol(true, whiteMoves, whiteMovesLength, whitePawnAttacks, whitePawnAttacksLength);
            f[8] = vulnerability(blackMoves, blackMovesLength, &gs) - vulnerability(whiteMoves, whiteMovesLength, &gs);
            f[9] = trapped(blackMoves, blackMovesLength, whiteMoves, whiteMovesLength, whitePawnTargets, whitePawnTargetsLength, whiteCoverage, whiteCoverageLength, &gs) - trapped(whiteMoves, whiteMovesLength, blackMoves, blackMovesLength, blackPawnTargets, blackPawnTargetsLength, blackCoverage, blackCoverageLength, &gs);
            f[10] = pins(blackMaterial, blackMaterialLength, whiteMoves, whiteMovesLength, blackCoverage, blackCoverageLength, whiteCoverage, whiteCoverageLength, &gs) - pins(whiteMaterial, whiteMaterialLength, blackMoves, blackMovesLength, whiteCoverage, whiteCoverageLength, blackCoverage, blackCoverageLength, &gs);
          }

        phase_alphas(&gs, phaseWeights);

        printf("{\"features\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],"
               "\"phase_alphas\":[%.9g,%.9g,%.9g]}\n",
               f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],
               phaseWeights[OPENING_GAME], phaseWeights[MIDDLE_GAME], phaseWeights[END_GAME]);
        return 0;
      }

    if(strcmp(cmd, "terminal") == 0)
      {
        is_term = terminal(&gs) ? 1 : 0;

        if(!is_term)
          {
            printf("{\"terminal\":false}\n");
          }
        else
          {
            res = isWin(&gs);
            if(res == GAME_OVER_STALEMATE)
              printf("{\"terminal\":true,\"result\":\"draw\"}\n");
            else
              printf("{\"terminal\":true,\"result\":\"loss\"}\n");
          }
        return 0;
      }

    if(strcmp(cmd, "legal_moves") == 0)
      {
        movesLen = getMoves(&gs, moves);

        printf("{\"moves_hex\":[");                                 //  Emit JSON.
        for(i = 0; i < movesLen; i++)
          {
            move_hex_encode_3(moves + i, mh);
            printf("\"%s\"%s", mh, (i + 1 < movesLen) ? "," : "");
          }
        printf("]}\n");
        return 0;
      }

    if(strcmp(cmd, "apply_move") == 0)
      {
        mv_i = json_find_top_value(line, toks, ntok, "move_hex");
        if(mv_i < 0 || toks[mv_i].type != JSMN_STRING)
          {
            fprintf(stderr, "Missing/invalid 'move_hex'.\n");
            return 6;
          }

        if(!tok_copy_string(line, &toks[mv_i], move_hex, sizeof(move_hex)))
          {
            fprintf(stderr, "'move_hex' wrong length.\n");
            return 6;
          }

        if(!move_hex_decode_3(move_hex, &mv))
          {
            fprintf(stderr, "Bad hex in 'move_hex'.\n");
            return 6;
          }

        makeMove(&mv, &gs);
        serialize(&gs, next_state);

        hex_encode(next_state, _GAMESTATE_BYTE_SIZE, hex);
        printf("{\"state_hex\":\"%s\"}\n", hex);
        return 0;
      }

    if(strcmp(cmd, "phase") == 0 || strcmp(cmd, "phase_alphas") == 0)
      {
        phase_alphas(&gs, phaseWeights);
        printf("{\"phase_alphas\":[%.9g,%.9g,%.9g]}\n",
               phaseWeights[OPENING_GAME], phaseWeights[MIDDLE_GAME], phaseWeights[END_GAME]);
        return 0;
      }

    fprintf(stderr, "Unknown cmd: %s\n", cmd);
    return 7;
  }

//  Compare a token to a literal string (token is not null-terminated).
static int tok_eq(const char* json, const jsmntok_t* tok, const char* s)
  {
    int len = (int)strlen(s);
    int tlen = tok->end - tok->start;
    return (tok->type == JSMN_STRING && tlen == len && strncmp(json + tok->start, s, (size_t)len) == 0);
  }

//  Copy token string into out (null-terminated). Returns 1 on success.
static int tok_copy_string(const char* json, const jsmntok_t* tok, char* out, size_t out_cap)
  {
    int tlen = tok->end - tok->start;
    if((size_t)tlen + 1 > out_cap)
      return 0;
    memcpy(out, json + tok->start, (size_t)tlen);
    out[tlen] = '\0';
    return 1;
  }

//  Find the token index of value for a given key in the top-level object.
//  Returns value token index, or -1 if not found.
static int json_find_top_value(const char* json, const jsmntok_t* toks, int ntok, const char* key)
  {
    if(ntok < 1 || toks[0].type != JSMN_OBJECT)
      return -1;
                                                                    //  jsmn stores object as: { key, value, key, value, ... } in tokens after toks[0]
    int i = 1;
    int pairs = toks[0].size;
    for(int p = 0; p < pairs; p++)
      {
        const jsmntok_t *k = &toks[i];
        const jsmntok_t *v = &toks[i + 1];
        if(tok_eq(json, k, key))
          {
            return i + 1;
          }
                                                                    //  Advance i to next key. BUT: value can be an object/array with nested tokens.
                                                                    //  We need to skip over the entire value subtree.
        i += 2;
                                                                    //  If v is a primitive/string, skipping is already done.
                                                                    //  If v is object/array, skip its nested tokens:
        if(v->type == JSMN_OBJECT || v->type == JSMN_ARRAY)
          {
                                                                    //  Skip over all descendant tokens (simple walker).
            int to_skip = 1;
            while(to_skip > 0 && i < ntok)
              {
                if(toks[i].type == JSMN_OBJECT || toks[i].type == JSMN_ARRAY)
                  {
                    to_skip += toks[i].size;
                  }
                to_skip--;
                i++;
              }
          }
      }
    return -1;
  }

static int hex_nibble(char c)
  {
    if('0' <= c && c <= '9')
      return c - '0';
    c = (char)tolower((unsigned char)c);
    if('a' <= c && c <= 'f')
      return 10 + (c - 'a');
    return -1;
  }

static int hex_decode_42(const char* hex, uint8_t out[42])
  {
    if(!hex)
      return 0;
    if(strlen(hex) != 84)                                           //  Must be exactly 84 hex chars.
      return 0;
    for(int i = 0; i < 42; i++)
      {
        int hi = hex_nibble(hex[2 * i]);
        int lo = hex_nibble(hex[2 * i + 1]);
        if(hi < 0 || lo < 0)
          return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
      }
    return 1;
  }

static void hex_encode(const uint8_t* in, size_t n, char* out_hex)
  {
    static const char *digits = "0123456789abcdef";
    for(size_t i = 0; i < n; i++)
      {
        out_hex[2 * i]     = digits[(in[i] >> 4) & 0xF];
        out_hex[2 * i + 1] = digits[in[i] & 0xF];
      }
    out_hex[2 * n] = '\0';
  }

static int move_hex_decode_3(const char *hex, Move* mv)
  {
    uint8_t out_move[3];
    if(!hex)
      return 0;
    if(strlen(hex) != 6)
      return 0;
    for(int i = 0; i < 3; i++)
      {
        int hi = hex_nibble(hex[2*i]);
        int lo = hex_nibble(hex[2*i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out_move[i] = (uint8_t)((hi << 4) | lo);
      }
    mv->from = out_move[0];
    mv->to = out_move[1];
    mv->promo = out_move[2];
    return 1;
  }

static void move_hex_encode_3(Move* mv, char out_hex[7])
  {
    static const char *d = "0123456789abcdef";

    out_hex[0] = d[(mv->from >> 4) & 0xF];
    out_hex[1] = d[mv->from & 0xF];

    out_hex[2] = d[(mv->to >> 4) & 0xF];
    out_hex[3] = d[mv->to & 0xF];

    out_hex[4] = d[(mv->promo >> 4) & 0xF];
    out_hex[5] = d[mv->promo & 0xF];

    out_hex[6] = '\0';
  }

/* Pack a GameState into the unsigned-char buffer. */
void serialize(GameState* gs, unsigned char* buffer)
  {
    unsigned char i = 0;
    unsigned char ch;

    unsigned char wPawnPtr = ENCODE_OFFSET_WHITE_PAWNS;
    unsigned char bPawnPtr = ENCODE_OFFSET_BLACK_PAWNS;
    unsigned char wKnightPtr = ENCODE_OFFSET_WHITE_KNIGHTS;
    unsigned char bKnightPtr = ENCODE_OFFSET_BLACK_KNIGHTS;
    unsigned char wBishopPtr = ENCODE_OFFSET_WHITE_BISHOPS;
    unsigned char bBishopPtr = ENCODE_OFFSET_BLACK_BISHOPS;
    unsigned char wRookPtr = ENCODE_OFFSET_WHITE_ROOKS;
    unsigned char bRookPtr = ENCODE_OFFSET_BLACK_ROOKS;

    //////////////////////////////////////////////////////////////////  (1 byte) Encode side to move and castling data.
                                                                    //  Byte [0].
    ch = 0;
    if(gs->whiteToMove)                                             //  Set high bit: white to move.
      ch = 128;

    //////////////////////////////////////////////////////////////////           Encode previous pawn double move data.
    ch += gs->previousDoublePawnMove;
    buffer[0] = ch;                                                 //  Write byte.

    //////////////////////////////////////////////////////////////////  Encode the board with a single loop.
                                                                    //  First initialize everything to "gone".
    for(i = ENCODE_OFFSET_WHITE_PAWNS; i < _GAMESTATE_BYTE_SIZE; i++)
      buffer[i] = _NONE;

    for(i = 0; i < _NONE; i++)
      {
        if(isWhite(i, gs))
          {
            if(isPawn(i, gs))
              {
                buffer[wPawnPtr] = i;
                if(wPawnPtr < ENCODE_OFFSET_BLACK_PAWNS)            //  Don't write over the next sub-section.
                  wPawnPtr++;
              }
            if(isKnight(i, gs))
              {
                buffer[wKnightPtr] = i;
                if(wKnightPtr < ENCODE_OFFSET_BLACK_KNIGHTS)        //  Don't write over the next sub-section.
                  wKnightPtr++;
              }
            if(isBishop(i, gs))
              {
                buffer[wBishopPtr] = i;
                if(wBishopPtr < ENCODE_OFFSET_BLACK_BISHOPS)        //  Don't write over the next sub-section.
                  wBishopPtr++;
              }
            if(isRook(i, gs))
              {
                buffer[wRookPtr] = i;
                if(wRookPtr < ENCODE_OFFSET_BLACK_ROOKS)            //  Don't write over the next sub-section.
                  wRookPtr++;
              }
            if(isCardinal(i, gs))
              buffer[ENCODE_OFFSET_WHITE_CARDINAL] = i;
            if(isMarshal(i, gs))
              buffer[ENCODE_OFFSET_WHITE_MARSHAL] = i;
            if(isQueen(i, gs))
              buffer[ENCODE_OFFSET_WHITE_QUEEN] = i;
            if(isKing(i, gs))
              buffer[ENCODE_OFFSET_WHITE_KING] = i;
          }
        else if(isBlack(i, gs))
          {
            if(isPawn(i, gs))
              {
                buffer[bPawnPtr] = i;
                if(bPawnPtr < ENCODE_OFFSET_WHITE_KNIGHTS)          //  Don't write over the next sub-section.
                  bPawnPtr++;
              }
            if(isKnight(i, gs))
              {
                buffer[bKnightPtr] = i;
                if(bKnightPtr < ENCODE_OFFSET_WHITE_BISHOPS)        //  Don't write over the next sub-section.
                  bKnightPtr++;
              }
            if(isBishop(i, gs))
              {
                buffer[bBishopPtr] = i;
                if(bBishopPtr < ENCODE_OFFSET_WHITE_ROOKS)          //  Don't write over the next sub-section.
                  bBishopPtr++;
              }
            if(isRook(i, gs))
              {
                buffer[bRookPtr] = i;
                if(bRookPtr < ENCODE_OFFSET_WHITE_CARDINAL)         //  Don't write over the next sub-section.
                  bRookPtr++;
              }
            if(isCardinal(i, gs))
              buffer[ENCODE_OFFSET_BLACK_CARDINAL] = i;
            if(isMarshal(i, gs))
              buffer[ENCODE_OFFSET_BLACK_MARSHAL] = i;
            if(isQueen(i, gs))
              buffer[ENCODE_OFFSET_BLACK_QUEEN] = i;
            if(isKing(i, gs))
              buffer[ENCODE_OFFSET_BLACK_KING] = i;
          }
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the move counter.
                                                                    //  Byte [41].
    buffer[ENCODE_OFFSET_MOVE_CTR] = gs->moveCtr;

    return;                                                         //  TOTAL: 42 bytes.
  }

/* Recover a GameState from the unsigned-char buffer". */
void deserialize(const uint8_t* buffer, GameState* gs)
  {
    unsigned char i;

    for(i = 0; i < _NONE; i++)                                      //  Fill-in/blank-out.
      gs->board[i] = _EMPTY;
    gs->previousDoublePawnMove = 0;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode side to move.
    gs->whiteToMove = ((buffer[0] & 128) == 128);                   //  Recover side to move from the high bit.
                                                                    //  Recover en-passant data.
    gs->previousDoublePawnMove = ((buffer[0] & 128) == 128) ? buffer[0] - 128 : buffer[0];
    if(gs->previousDoublePawnMove > 10)
      gs->previousDoublePawnMove = 0;                               //  "There can be only one!"

    //////////////////////////////////////////////////////////////////  Decode board.
    for(i = ENCODE_OFFSET_WHITE_PAWNS; i < ENCODE_OFFSET_BLACK_PAWNS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _WHITE_PAWN;
      }
    for(i = ENCODE_OFFSET_BLACK_PAWNS; i < ENCODE_OFFSET_WHITE_KNIGHTS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _BLACK_PAWN;
      }
    for(i = ENCODE_OFFSET_WHITE_KNIGHTS; i < ENCODE_OFFSET_BLACK_KNIGHTS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _WHITE_KNIGHT;
      }
    for(i = ENCODE_OFFSET_BLACK_KNIGHTS; i < ENCODE_OFFSET_WHITE_BISHOPS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _BLACK_KNIGHT;
      }
    for(i = ENCODE_OFFSET_WHITE_BISHOPS; i < ENCODE_OFFSET_BLACK_BISHOPS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _WHITE_BISHOP;
      }
    for(i = ENCODE_OFFSET_BLACK_BISHOPS; i < ENCODE_OFFSET_WHITE_ROOKS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _BLACK_BISHOP;
      }
    for(i = ENCODE_OFFSET_WHITE_ROOKS; i < ENCODE_OFFSET_BLACK_ROOKS; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _WHITE_ROOK;
      }
    for(i = ENCODE_OFFSET_BLACK_ROOKS; i < ENCODE_OFFSET_WHITE_CARDINAL; i++)
      {
        if(buffer[i] < _NONE)
          gs->board[ buffer[i] ] = _BLACK_ROOK;
      }

    if(buffer[ENCODE_OFFSET_WHITE_CARDINAL] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_WHITE_CARDINAL] ] = _WHITE_CARDINAL;
    if(buffer[ENCODE_OFFSET_BLACK_CARDINAL] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_BLACK_CARDINAL] ] = _BLACK_CARDINAL;

    if(buffer[ENCODE_OFFSET_WHITE_MARSHAL] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_WHITE_MARSHAL] ] = _WHITE_MARSHAL;
    if(buffer[ENCODE_OFFSET_BLACK_MARSHAL] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_BLACK_MARSHAL] ] = _BLACK_MARSHAL;

    if(buffer[ENCODE_OFFSET_WHITE_QUEEN] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_WHITE_QUEEN] ] = _WHITE_QUEEN;
    if(buffer[ENCODE_OFFSET_BLACK_QUEEN] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_BLACK_QUEEN] ] = _BLACK_QUEEN;

    if(buffer[ENCODE_OFFSET_WHITE_KING] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_WHITE_KING] ] = _WHITE_KING;
    if(buffer[ENCODE_OFFSET_BLACK_KING] < _NONE)
      gs->board[ buffer[ENCODE_OFFSET_BLACK_KING] ] = _BLACK_KING;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the move counter.
    gs->moveCtr = buffer[ENCODE_OFFSET_MOVE_CTR];

    return;                                                         //  TOTAL: 42 bytes.
  }
