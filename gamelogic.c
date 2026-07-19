/*

Game logic module for the human player.

sudo docker run --rm -v $(pwd):/src -u $(id -u):$(id -g) --mount type=bind,source=$(pwd),target=/home/src c-wasm emcc -Os -s STANDALONE_WASM -s EXPORTED_FUNCTIONS="['_getCurrentState','_getMovesBuffer','_sideToMove_client','_isWhite_client','_isBlack_client','_isEmpty_client','_isPawn_client','_isKnight_client','_isBishop_client','_isRook_client','_isCardinal_client','_isMarshal_client','_isQueen_client','_isKing_client','_getMovesIndex_client','_makeMove_client','_canRefusePromotion_client','_canPromoteToKnight_client','_canPromoteToBishop_client','_canPromoteToRook_client','_canPromoteToCardinal_client','_canPromoteToMarshal_client','_canPromoteToQueen_client','_isTerminal_client','_isWin_client','_draw']" -Wl,--no-entry "gamelogic.c" -o "gamelogic.wasm"

*/

#include "gamestate.h"

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

/**************************************************************************************************
 Typedefs  */


/**************************************************************************************************
 Prototypes  */

__attribute__((import_module("env"), import_name("_printRow"))) void printRow(char a, char b, char c, char d, char e, char f, char g, char h, char i, char j);
__attribute__((import_module("env"), import_name("_printGameStateData"))) void printGameStateData(bool wToMove, unsigned char previousDoubleMoveColumn, unsigned char moveCtr);
unsigned char* getCurrentState(void);
unsigned char* getMovesBuffer(void);
void serialize(GameState*);
void deserialize(GameState*);

unsigned char sideToMove_client(void);
bool isWhite_client(unsigned char);
bool isBlack_client(unsigned char);
bool isEmpty_client(unsigned char);
bool isPawn_client(unsigned char);
bool isKnight_client(unsigned char);
bool isBishop_client(unsigned char);
bool isRook_client(unsigned char);
bool isCardinal_client(unsigned char);
bool isMarshal_client(unsigned char);
bool isQueen_client(unsigned char);
bool isKing_client(unsigned char);

unsigned int getMovesIndex_client(unsigned char);
void makeMove_client(unsigned char, unsigned char, unsigned char);

bool canRefusePromotion_client(unsigned char);
bool canPromoteToKnight_client(bool);
bool canPromoteToBishop_client(bool);
bool canPromoteToRook_client(bool);
bool canPromoteToCardinal_client(bool);
bool canPromoteToMarshal_client(bool);
bool canPromoteToQueen_client(bool);

bool isTerminal_client(void);
unsigned char isWin_client(void);

void draw(void);

/**************************************************************************************************
 Globals  */

unsigned char currentState[_GAMESTATE_BYTE_SIZE];                   //  Global array containing the serialized game state.
unsigned char movesBuffer[_MAX_NUM_TARGETS];                        //  Global array containing the unique destination-indices
                                                                    //  (not necessarily the number of unique moves) available.

/**************************************************************************************************
 Functions  */

/* Expose the global array declared here to JavaScript.  */
unsigned char* getCurrentState(void)
  {
    return &currentState[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getMovesBuffer(void)
  {
    return &movesBuffer[0];
  }

/* Game State Encoding & Decoding

   Byte [     0] = Side to move and en-passant data: [7][6][5][4][3][2][1][0]
                                                      ^  ^  ^  ^  ^  ^  ^  ^
                                                      |  |  |  |  |  |  |  +--- { Remaining }
                                                      |  |  |  |  |  |  +------ { bits      }
                                                      |  |  |  |  |  +--------- { encode    }
                                                      |  |  |  |  +------------ { values    }
                                                      |  |  |  +--------------- { in the    }
                                                      |  |  +------------------ { range of  }
                                                      |  +--------------------- { [0, 10].  }
                                                      +------------------------ ON: white to move; OFF: black to move.
   Bytes[ 1, 10] = Positions of white pawns.
   Bytes[11, 20] = Positions of black pawns.
   Bytes[21, 22] = Positions of white knights.
   Bytes[23, 24] = Positions of black knights.
   Bytes[25, 26] = Positions of white bishops.
   Bytes[27, 28] = Positions of black bishops.
   Bytes[29, 30] = Positions of white rooks.
   Bytes[31, 32] = Positions of black rooks.
   Byte [    33] = Position of white cardinal.
   Byte [    34] = Position of black cardinal.
   Byte [    35] = Position of white marshal.
   Byte [    36] = Position of black marshal.
   Byte [    37] = Position of white queen.
   Byte [    38] = Position of black queen.
   Byte [    39] = Position of white king.
   Byte [    40] = Position of black king.
   Byte [    41] = Move counter                      */

/* Pack a GameState into the unsigned-char buffer "currentState". */
void serialize(GameState* gs)
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
    currentState[0] = ch;                                           //  Write byte.

    //////////////////////////////////////////////////////////////////  Encode the board with a single loop.
                                                                    //  First initialize everything to "gone".
    for(i = ENCODE_OFFSET_WHITE_PAWNS; i < _GAMESTATE_BYTE_SIZE; i++)
      currentState[i] = _NONE;

    for(i = 0; i < _NONE; i++)
      {
        if(isWhite(i, gs))
          {
            if(isPawn(i, gs))
              {
                currentState[wPawnPtr] = i;
                if(wPawnPtr < ENCODE_OFFSET_BLACK_PAWNS)            //  Don't write over the next sub-section.
                  wPawnPtr++;
              }
            if(isKnight(i, gs))
              {
                currentState[wKnightPtr] = i;
                if(wKnightPtr < ENCODE_OFFSET_BLACK_KNIGHTS)        //  Don't write over the next sub-section.
                  wKnightPtr++;
              }
            if(isBishop(i, gs))
              {
                currentState[wBishopPtr] = i;
                if(wBishopPtr < ENCODE_OFFSET_BLACK_BISHOPS)        //  Don't write over the next sub-section.
                  wBishopPtr++;
              }
            if(isRook(i, gs))
              {
                currentState[wRookPtr] = i;
                if(wRookPtr < ENCODE_OFFSET_BLACK_ROOKS)            //  Don't write over the next sub-section.
                  wRookPtr++;
              }
            if(isCardinal(i, gs))
              currentState[ENCODE_OFFSET_WHITE_CARDINAL] = i;
            if(isMarshal(i, gs))
              currentState[ENCODE_OFFSET_WHITE_MARSHAL] = i;
            if(isQueen(i, gs))
              currentState[ENCODE_OFFSET_WHITE_QUEEN] = i;
            if(isKing(i, gs))
              currentState[ENCODE_OFFSET_WHITE_KING] = i;
          }
        else if(isBlack(i, gs))
          {
            if(isPawn(i, gs))
              {
                currentState[bPawnPtr] = i;
                if(bPawnPtr < ENCODE_OFFSET_WHITE_KNIGHTS)          //  Don't write over the next sub-section.
                  bPawnPtr++;
              }
            if(isKnight(i, gs))
              {
                currentState[bKnightPtr] = i;
                if(bKnightPtr < ENCODE_OFFSET_WHITE_BISHOPS)        //  Don't write over the next sub-section.
                  bKnightPtr++;
              }
            if(isBishop(i, gs))
              {
                currentState[bBishopPtr] = i;
                if(bBishopPtr < ENCODE_OFFSET_WHITE_ROOKS)          //  Don't write over the next sub-section.
                  bBishopPtr++;
              }
            if(isRook(i, gs))
              {
                currentState[bRookPtr] = i;
                if(bRookPtr < ENCODE_OFFSET_WHITE_CARDINAL)         //  Don't write over the next sub-section.
                  bRookPtr++;
              }
            if(isCardinal(i, gs))
              currentState[ENCODE_OFFSET_BLACK_CARDINAL] = i;
            if(isMarshal(i, gs))
              currentState[ENCODE_OFFSET_BLACK_MARSHAL] = i;
            if(isQueen(i, gs))
              currentState[ENCODE_OFFSET_BLACK_QUEEN] = i;
            if(isKing(i, gs))
              currentState[ENCODE_OFFSET_BLACK_KING] = i;
          }
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the move counter.
                                                                    //  Byte [41].
    currentState[ENCODE_OFFSET_MOVE_CTR] = gs->moveCtr;

    return;                                                         //  TOTAL: 42 bytes.
  }

/* Recover a GameState from the unsigned-char buffer "currentState". */
void deserialize(GameState* gs)
  {
    unsigned char i;

    for(i = 0; i < _NONE; i++)                                      //  Fill-in/blank-out.
      gs->board[i] = _EMPTY;
    gs->previousDoublePawnMove = 0;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode side to move.
    gs->whiteToMove = ((currentState[0] & 128) == 128);             //  Recover side to move from the high bit.
                                                                    //  Recover en-passant data.
    gs->previousDoublePawnMove = ((currentState[0] & 128) == 128) ? currentState[0] - 128 : currentState[0];
    if(gs->previousDoublePawnMove > 10)
      gs->previousDoublePawnMove = 0;                               //  "There can be only one!"

    //////////////////////////////////////////////////////////////////  Decode board.
    for(i = ENCODE_OFFSET_WHITE_PAWNS; i < ENCODE_OFFSET_BLACK_PAWNS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _WHITE_PAWN;
      }
    for(i = ENCODE_OFFSET_BLACK_PAWNS; i < ENCODE_OFFSET_WHITE_KNIGHTS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _BLACK_PAWN;
      }
    for(i = ENCODE_OFFSET_WHITE_KNIGHTS; i < ENCODE_OFFSET_BLACK_KNIGHTS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _WHITE_KNIGHT;
      }
    for(i = ENCODE_OFFSET_BLACK_KNIGHTS; i < ENCODE_OFFSET_WHITE_BISHOPS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _BLACK_KNIGHT;
      }
    for(i = ENCODE_OFFSET_WHITE_BISHOPS; i < ENCODE_OFFSET_BLACK_BISHOPS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _WHITE_BISHOP;
      }
    for(i = ENCODE_OFFSET_BLACK_BISHOPS; i < ENCODE_OFFSET_WHITE_ROOKS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _BLACK_BISHOP;
      }
    for(i = ENCODE_OFFSET_WHITE_ROOKS; i < ENCODE_OFFSET_BLACK_ROOKS; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _WHITE_ROOK;
      }
    for(i = ENCODE_OFFSET_BLACK_ROOKS; i < ENCODE_OFFSET_WHITE_CARDINAL; i++)
      {
        if(currentState[i] < _NONE)
          gs->board[ currentState[i] ] = _BLACK_ROOK;
      }

    if(currentState[ENCODE_OFFSET_WHITE_CARDINAL] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_WHITE_CARDINAL] ] = _WHITE_CARDINAL;
    if(currentState[ENCODE_OFFSET_BLACK_CARDINAL] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_BLACK_CARDINAL] ] = _BLACK_CARDINAL;

    if(currentState[ENCODE_OFFSET_WHITE_MARSHAL] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_WHITE_MARSHAL] ] = _WHITE_MARSHAL;
    if(currentState[ENCODE_OFFSET_BLACK_MARSHAL] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_BLACK_MARSHAL] ] = _BLACK_MARSHAL;

    if(currentState[ENCODE_OFFSET_WHITE_QUEEN] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_WHITE_QUEEN] ] = _WHITE_QUEEN;
    if(currentState[ENCODE_OFFSET_BLACK_QUEEN] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_BLACK_QUEEN] ] = _BLACK_QUEEN;

    if(currentState[ENCODE_OFFSET_WHITE_KING] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_WHITE_KING] ] = _WHITE_KING;
    if(currentState[ENCODE_OFFSET_BLACK_KING] < _NONE)
      gs->board[ currentState[ENCODE_OFFSET_BLACK_KING] ] = _BLACK_KING;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the move counter.
    gs->moveCtr = currentState[ENCODE_OFFSET_MOVE_CTR];

    return;                                                         //  TOTAL: 42 bytes.
  }

/* Answer the client-side question, Whose turn is it? */
unsigned char sideToMove_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return (gs.whiteToMove) ? _WHITE_TO_MOVE : _BLACK_TO_MOVE;
  }

bool isWhite_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isWhite(index, &gs);
  }

bool isBlack_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isBlack(index, &gs);
  }

bool isEmpty_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isEmpty(index, &gs);
  }

bool isPawn_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isPawn(index, &gs);
  }

bool isKnight_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isKnight(index, &gs);
  }

bool isBishop_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isBishop(index, &gs);
  }

bool isRook_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isRook(index, &gs);
  }

bool isCardinal_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isCardinal(index, &gs);
  }

bool isMarshal_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isMarshal(index, &gs);
  }

bool isQueen_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isQueen(index, &gs);
  }

bool isKing_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isKing(index, &gs);
  }

/* Can the pawn at "index" refuse promotion? */
bool canRefusePromotion_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isPawn(index, &gs) && ((isWhite(index, &gs) && row(index) < 9) || (isBlack(index, &gs) && row(index) > 0));
  }

/* Can the team indicated promote to a knight? */
bool canPromoteToKnight_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamKnightsOnBoard('w', &gs) : teamKnightsOnBoard('b', &gs);
    return num < 2;
  }

/* Can the team indicated promote to a bishop? */
bool canPromoteToBishop_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamBishopsOnBoard('w', &gs) : teamBishopsOnBoard('b', &gs);
    return num < 2;
  }

/* Can the team indicated promote to a rook? */
bool canPromoteToRook_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamRooksOnBoard('w', &gs) : teamRooksOnBoard('b', &gs);
    return num < 2;
  }

/* Can the team indicated promote to a cardinal? */
bool canPromoteToCardinal_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamCardinalsOnBoard('w', &gs) : teamCardinalsOnBoard('b', &gs);
    return num < 1;
  }

/* Can the team indicated promote to a marshal? */
bool canPromoteToMarshal_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamMarshalsOnBoard('w', &gs) : teamMarshalsOnBoard('b', &gs);
    return num < 1;
  }

/* Can the team indicated promote to a queen? */
bool canPromoteToQueen_client(bool white)
  {
    GameState gs;
    unsigned char num;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    num = white ? teamQueensOnBoard('w', &gs) : teamQueensOnBoard('b', &gs);
    return num < 1;
  }

bool isTerminal_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return terminal(&gs);
  }

/* Returns unsigned char in {GAME_ONGOING         = 0,
                             GAME_OVER_WHITE_WINS = 1,
                             GAME_OVER_BLACK_WINS = 2,
                             GAME_OVER_STALEMATE  = 3}. */
unsigned char isWin_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isWin(&gs);
  }

/* Given an index, recover the game state from the global buffer "currentState", and compute the moves available to the piece at "index."
   The number of moves is returned, and that many bytes in "movesBuffer" will contain a destinations.
   This function is intended to answer queries from the human player. */
unsigned int getMovesIndex_client(unsigned char index)
  {
    GameState gs;
    Move moves[_MAX_NUM_TARGETS];                                   //  Generous upper bound assumes that a single piece could reach half of all squares.
    unsigned int len, i = 0, j;
    unsigned int ctr;
    unsigned char indices[_MAX_NUM_TARGETS];

    deserialize(&gs);                                               //  Recover GameState from buffer.
    len = getMovesIndex(index, &gs, moves);

    ctr = 0;
    for(i = 0; i < len; i++)                                        //  Iterate through moves for index and identify unique destination indices.
      {
        j = 0;
        while(j < ctr && indices[j] != moves[i].to)
          j++;
        if(j == ctr)
          indices[ctr++] = moves[i].to;
      }

    i = 0;                                                          //  Reset. 'i' now iterates into 'movesBuffer'.
    for(len = 0; len < ctr; len++)
      movesBuffer[i++] = indices[len];

    return ctr;
  }

/* Update "currentState" according to the given move data (if those data are indeed valid!) */
void makeMove_client(unsigned char from, unsigned char to, unsigned char promo)
  {
    GameState gs;
    Move moves[_NONE];                                              //  Generous assumption that every square is reachable.
    Move move;
    unsigned int len, i;

    deserialize(&gs);                                               //  Recover GameState from buffer.
    len = getMovesIndex(from, &gs, moves);                          //  Make sure that this move is legal.
    i = 0;                                                          //  Otherwise, ignore it. Cheaters lose their turns!
    while(i < len && !(moves[i].from == from && moves[i].to == to && moves[i].promo == promo))
      i++;
    if(i < len)
      {
        move.from = from;
        move.to = to;
        move.promo = promo;
        makeMove(&move, &gs);
      }

    serialize(&gs);                                                 //  Write updated GameState back to buffer.

    return;
  }

/* Draw the board to the JavaScript console.
   r . . . . . . . . r
   . n b q k m c b n .
   p p p p p p p p p p
   . . . . . . . . . .
   . . . . . . . . . .
   . . . . . . . . . .
   . . . . . . . . . .
   P P P P P P P P P P
   . N B Q K M C B N .
   R . . . . . . . . R */
void draw(void)
  {
    GameState gs;
    signed char y;

    deserialize(&gs);                                               //  Recover GameState from buffer.

    for(y = 9; y >= 0; y--)
      printRow(gs.board[y * 10], gs.board[y * 10 + 1], gs.board[y * 10 + 2], gs.board[y * 10 + 3], gs.board[y * 10 + 4], gs.board[y * 10 + 5], gs.board[y * 10 + 6], gs.board[y * 10 + 7], gs.board[y * 10 + 8], gs.board[y * 10 + 9]);

    printGameStateData(gs.whiteToMove, gs.previousDoublePawnMove, gs.moveCtr);
    return;
  }
