/*

sudo docker run --rm -v $(pwd):/src -u $(id -u):$(id -g) --mount type=bind,source=$(pwd),target=/home/src c-wasm emcc -Os -s STANDALONE_WASM -s EXPORTED_FUNCTIONS="['_getInputGameStateBuffer','_getInputMoveBuffer','_getOutputGameStateBuffer','_getOutputMovesBuffer','_sideToMove_eval','_isTerminal_eval','_isSideToMoveInCheck_eval','_nonPawnMaterial_eval','_makeMove_eval','_makeNullMove_eval','_evaluate_eval','_getMoves_eval']" -Wl,--no-entry "ambrosia.c" -o "eval.wasm"

*/

#include "gamestate.h"
#include "ambrosia.h"

#define SEE_SCORE_PAWN                8                             /* Static Exchange Evaluation, rough pawn score. */
#define SEE_SCORE_KNIGHT             28                             /* Static Exchange Evaluation, rough knight score. */
#define SEE_SCORE_BISHOP             33                             /* Static Exchange Evaluation, rough bishop score. */
#define SEE_SCORE_ROOK               50                             /* Static Exchange Evaluation, rough rook score. */
#define SEE_SCORE_CARDINAL           85                             /* Static Exchange Evaluation, rough cardinal score. */
#define SEE_SCORE_MARSHAL            90                             /* Static Exchange Evaluation, rough marshal score. */
#define SEE_SCORE_QUEEN              95                             /* Static Exchange Evaluation, rough queen score. */
#define SEE_SCORE_KING             1000                             /* Static Exchange Evaluation, rough king score. */
#define MOVE_SORTING_PROMO_BONUS    800                             /* Static Exchange Evaluation, rough promotion bonus. */
#define MOVE_SORTING_CHECK_BONUS     50                             /* Static Exchange Evaluation, rough putting-opponent-in-check bonus. */

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

unsigned char* getInputGameStateBuffer(void);
unsigned char* getInputMoveBuffer(void);
unsigned char* getOutputGameStateBuffer(void);
unsigned char* getOutputMovesBuffer(void);

void serializeGameStateToBuffer(GameState*, unsigned char*);
void serializeMoveToBuffer(Move*, unsigned char*);
void deserializeGameState(GameState*);
void deserializeMove(Move*);

unsigned char sideToMove_eval(void);
bool isTerminal_eval(void);
bool isSideToMoveInCheck_eval(void);
unsigned char nonPawnMaterial_eval(void);
void makeMove_eval(void);
void makeNullMove_eval(void);
float evaluate_eval(void);
unsigned int getMoves_eval(void);
signed int SEE(Move*, GameState*);
signed int SEE_lookup(char);

/**************************************************************************************************
 Globals  */

unsigned char inputGameStateBuffer[_GAMESTATE_BYTE_SIZE];           //  Global array containing the serialized INPUT game state.

unsigned char inputMoveBuffer[_MOVE_BYTE_SIZE];                     //  Global array containing the serialized INPUT move.

unsigned char outputGameStateBuffer[_GAMESTATE_BYTE_SIZE];          //  Global array containing the serialized OUTPUT game state.

                                                                    //  Global array containing up to _MAX_MOVES moves.
                                                                    //  Rather than encode the number of moves in the array itself, we return an integer.
                                                                    //  Each move is represented as a byte sub-array encoding:
                                                                    //    _MOVE_BYTE_SIZE  :  bytes encoding a single move,
                                                                    //    4                :  bytes for signed integer, which is rough score.
unsigned char outputMovesBuffer[_MAX_MOVES * (_MOVE_BYTE_SIZE + 5)];//    1                :  byte (should be Boolean) indicating whether move is "quiet".

/**************************************************************************************************
 Functions  */

/* Expose the global array declared here to JavaScript.  */
unsigned char* getInputGameStateBuffer(void)
  {
    return &inputGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getInputMoveBuffer(void)
  {
    return &inputMoveBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getOutputGameStateBuffer(void)
  {
    return &outputGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getOutputMovesBuffer(void)
  {
    return &outputMovesBuffer[0];
  }

/* Write the given game state to the given buffer. */
void serializeGameStateToBuffer(GameState* gs, unsigned char* buffer)
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

/* Write the given move to the given buffer. */
void serializeMoveToBuffer(Move* move, unsigned char* buffer)
  {
    unsigned char i = 0;

    buffer[i++] = move->from;
    buffer[i++] = move->to;
    buffer[i++] = move->promo;

    return;
  }

/* Recover a GameState from the unsigned-char buffer "inputGameStateBuffer". */
void deserializeGameState(GameState* gs)
  {
    unsigned char i;

    for(i = 0; i < _NONE; i++)                                      //  Fill-in/blank-out.
      gs->board[i] = _EMPTY;
    gs->previousDoublePawnMove = 0;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode side to move.
    gs->whiteToMove = ((inputGameStateBuffer[0] & 128) == 128);     //  Recover side to move from the high bit.
                                                                    //  Recover en-passant data.
    gs->previousDoublePawnMove = ((inputGameStateBuffer[0] & 128) == 128) ? inputGameStateBuffer[0] - 128 : inputGameStateBuffer[0];
    if(gs->previousDoublePawnMove > 10)
      gs->previousDoublePawnMove = 0;                               //  "There can be only one!"

    //////////////////////////////////////////////////////////////////  Decode board.
    for(i = ENCODE_OFFSET_WHITE_PAWNS; i < ENCODE_OFFSET_BLACK_PAWNS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _WHITE_PAWN;
      }
    for(i = ENCODE_OFFSET_BLACK_PAWNS; i < ENCODE_OFFSET_WHITE_KNIGHTS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _BLACK_PAWN;
      }
    for(i = ENCODE_OFFSET_WHITE_KNIGHTS; i < ENCODE_OFFSET_BLACK_KNIGHTS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _WHITE_KNIGHT;
      }
    for(i = ENCODE_OFFSET_BLACK_KNIGHTS; i < ENCODE_OFFSET_WHITE_BISHOPS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _BLACK_KNIGHT;
      }
    for(i = ENCODE_OFFSET_WHITE_BISHOPS; i < ENCODE_OFFSET_BLACK_BISHOPS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _WHITE_BISHOP;
      }
    for(i = ENCODE_OFFSET_BLACK_BISHOPS; i < ENCODE_OFFSET_WHITE_ROOKS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _BLACK_BISHOP;
      }
    for(i = ENCODE_OFFSET_WHITE_ROOKS; i < ENCODE_OFFSET_BLACK_ROOKS; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _WHITE_ROOK;
      }
    for(i = ENCODE_OFFSET_BLACK_ROOKS; i < ENCODE_OFFSET_WHITE_CARDINAL; i++)
      {
        if(inputGameStateBuffer[i] < _NONE)
          gs->board[ inputGameStateBuffer[i] ] = _BLACK_ROOK;
      }

    if(inputGameStateBuffer[ENCODE_OFFSET_WHITE_CARDINAL] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_WHITE_CARDINAL] ] = _WHITE_CARDINAL;
    if(inputGameStateBuffer[ENCODE_OFFSET_BLACK_CARDINAL] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_BLACK_CARDINAL] ] = _BLACK_CARDINAL;

    if(inputGameStateBuffer[ENCODE_OFFSET_WHITE_MARSHAL] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_WHITE_MARSHAL] ] = _WHITE_MARSHAL;
    if(inputGameStateBuffer[ENCODE_OFFSET_BLACK_MARSHAL] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_BLACK_MARSHAL] ] = _BLACK_MARSHAL;

    if(inputGameStateBuffer[ENCODE_OFFSET_WHITE_QUEEN] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_WHITE_QUEEN] ] = _WHITE_QUEEN;
    if(inputGameStateBuffer[ENCODE_OFFSET_BLACK_QUEEN] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_BLACK_QUEEN] ] = _BLACK_QUEEN;

    if(inputGameStateBuffer[ENCODE_OFFSET_WHITE_KING] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_WHITE_KING] ] = _WHITE_KING;
    if(inputGameStateBuffer[ENCODE_OFFSET_BLACK_KING] < _NONE)
      gs->board[ inputGameStateBuffer[ENCODE_OFFSET_BLACK_KING] ] = _BLACK_KING;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the move counter.
    gs->moveCtr = inputGameStateBuffer[ENCODE_OFFSET_MOVE_CTR];

    return;                                                         //  TOTAL: 42 bytes.
  }

/* Recover a Move from the unsigned-char buffer "inputMoveBuffer". */
void deserializeMove(Move* move)
  {
    move->from = inputMoveBuffer[0];
    move->to = inputMoveBuffer[1];
    move->promo = inputMoveBuffer[2];
    return;
  }

/* Answer the Negamax Module's query, "Which side is to move in the GameState in the query buffer?"
   Return an unsigned char in {_WHITE_TO_MOVE, _BLACK_TO_MOVE}. */
unsigned char sideToMove_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return gs.whiteToMove ? _WHITE_TO_MOVE : _BLACK_TO_MOVE;
  }

/* Answer the Negamax Module's query, "Is the GameState in the query buffer terminal?" */
bool isTerminal_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return terminal(&gs);
  }

/* Answer the Negamax Module's query, "Is the side to move in the GameState in the query buffer in check?" */
bool isSideToMoveInCheck_eval(void)
  {
    GameState gs;
    unsigned char i;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    if(gs.whiteToMove)                                              //  White is to move; is white in check?
      {
        i = 0;
        while(i < _NONE && gs.board[i] != _WHITE_KING)
          i++;
        return inCheckBy(i, 'b', &gs);
      }
                                                                    //  Black is to move; is black in check?
    i = 0;
    while(i < _NONE && gs.board[i] != _BLACK_KING)
      i++;
    return inCheckBy(i, 'w', &gs);
  }

/* Answer the Negamax Module's query, "How much non-pawn material does the side to move have in the GameState in the query buffer?" */
unsigned char nonPawnMaterial_eval(void)
  {
    GameState gs;
    unsigned char knights = 0;
    unsigned char bishops = 0;
    unsigned char rooks = 0;
    unsigned char cardinals = 0;
    unsigned char marshals = 0;
    unsigned char queens = 0;
    unsigned char i;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    if(gs.whiteToMove)
      {
        for(i = 0; i < _NONE; i++)
          {
            if(isWhite(i, &gs))
              {
                if(isKnight(i, &gs))
                  knights++;
                else if(isBishop(i, &gs))
                  bishops++;
                else if(isRook(i, &gs))
                  rooks++;
                else if(isCardinal(i, &gs))
                  cardinals++;
                else if(isMarshal(i, &gs))
                  marshals++;
                else if(isQueen(i, &gs))
                  queens++;
              }
          }
      }
    else
      {
        for(i = 0; i < _NONE; i++)
          {
            if(isBlack(i, &gs))
              {
                if(isKnight(i, &gs))
                  knights++;
                else if(isBishop(i, &gs))
                  bishops++;
                else if(isRook(i, &gs))
                  rooks++;
                else if(isCardinal(i, &gs))
                  cardinals++;
                else if(isMarshal(i, &gs))
                  marshals++;
                else if(isQueen(i, &gs))
                  queens++;
              }
          }
      }

    return 3 * (knights + bishops) + 5 * rooks + 8 * cardinals + 9 * marshals + 9 * queens;
  }

/* Answer the Negamax Module's query, "What GameState results from making the move in the input-move buffer in the game state in the input-gamestate buffer?"
   Writes to "outputGameStateBuffer". */
void makeMove_eval(void)
  {
    GameState gs;
    Move move;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    deserializeMove(&move);                                         //  Recover Move from buffer.

    makeMove(&move, &gs);                                           //  Make the move.

    serializeGameStateToBuffer(&gs, outputGameStateBuffer);         //  Write updated GameState to output-gamestate buffer.

    return;
  }

/* For use by null-move pruning in tree-search.
   Answer the Negamax Module's query, "What GameState results from a null-move in the game state in the input-gamestate buffer?"
   Writes to "outputGameStateBuffer". */
void makeNullMove_eval(void)
  {
    GameState gs;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    makeNullMove(&gs);                                              //  Make the move.

    serializeGameStateToBuffer(&gs, outputGameStateBuffer);         //  Write updated GameState to output-gamestate buffer.

    return;
  }

/* Answer the Negamax Module's query, "What is the evaluation of the GameState in the input-gamestate buffer?" */
float evaluate_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return score(&gs);                                              //  Negamax rule: always evaluate for the side that is now to move.
  }

/* Answer the Negamax Module's query, "What are all the moves that can be made from the GameState in the input-gamestate buffer?"
   Writes to "outputMovesBuffer":
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"],
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"],
                                                         . . .
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"] */
unsigned int getMoves_eval()
  {
    GameState gs, child;
    Move moves[_MAX_MOVES];
    unsigned int movesLen = 0;
    signed int score, scores[_MAX_MOVES];                           //  Use fast, cheap heuristics like SEE.

    unsigned char buffer4[4];                                       //  Byte array to hold leading int = number of moves in output buffer.
    unsigned int i, j, k;
    unsigned char whiteKingIndex, blackKingIndex;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    movesLen = getMoves(&gs, moves);                                //  Get moves.

    for(i = 0; i < movesLen; i++)                                   //  Compute a fast-n-cheap score to help the Negamax Module sort its nodes.
      {
        scores[i] = 0;                                              //  Initialize every move to zero.

        if(isCapture(moves + i, &gs))                               //  Move is a capture.
          scores[i] += SEE(moves + i, &gs);                         //  Static Exchange Evaluation can reveal good, equal, or bad captures.
        if((moves + i)->promo != _NO_PROMO)                         //  Move is a promotion.
          scores[i] += MOVE_SORTING_PROMO_BONUS;

        copyGameState(&gs, &child);                                 //  Clone the source state.
        makeMove(moves + i, &child);                                //  Apply the candidate move.
        whiteKingIndex = 0;                                         //  Locate the white king.
        while(whiteKingIndex < _NONE && child.board[whiteKingIndex] != _WHITE_KING)
          whiteKingIndex++;
        blackKingIndex = 0;                                         //  Locate the black king.
        while(blackKingIndex < _NONE && child.board[blackKingIndex] != _BLACK_KING)
          blackKingIndex++;
                                                                    //  Move puts opponent in check?
        if( (!child.whiteToMove && inCheckBy(blackKingIndex, 'w', &child)) || (child.whiteToMove && inCheckBy(whiteKingIndex, 'b', &child)) )
          scores[i] += MOVE_SORTING_CHECK_BONUS;
      }

    i = 0;                                                          //  Point to head of output buffer.
    for(j = 0; j < movesLen; j++)                                   //  Write moves as bytes to output buffer, following uint total number of moves.
      {
        outputMovesBuffer[i++] = moves[j].from;                     //  Copy move to global byte array.
        outputMovesBuffer[i++] = moves[j].to;
        outputMovesBuffer[i++] = moves[j].promo;

        score = scores[j];
        memcpy(buffer4, (unsigned char*)(&score), 4);               //  Force the SIGNED integer into a 4-byte temp buffer.
        for(k = 0; k < 4; k++)                                      //  Copy local SIGNED score to global output byte array.
          outputMovesBuffer[i++] = buffer4[k];
                                                                    //  0: quiet; 1: capture or promotion.
        outputMovesBuffer[i++] = (moves[j].promo == _NO_PROMO && !isCapture(moves + j, &gs)) ? 0 : 1;
      }

    return movesLen;
  }

/* Static Exchange Evaluation */
signed int SEE(Move* move, GameState* src)
  {
    GameState gs;
    signed int gains[_NONE];
    unsigned char gainsLen = 0;
    unsigned char target;
    signed int capturedPieceVal;
    char capturingPiece;
    char team;
    Move buffer[_NONE];
    Move chosenMove;
    unsigned char len, i;
    signed int leastVal, val, victimVal, j;
    signed int stopHere, continueExchange;

    copyGameState(src, &gs);
    target = move->to;                                              //  Identify the target.

    capturedPieceVal = SEE_lookup(gs.board[move->to]);              //  Look up value of the captured piece.
    capturingPiece = gs.board[move->from];
    gains[0] = capturedPieceVal;
    makeMove(move, &gs);                                            //  Apply the move to be evaluated.
    team = gs.whiteToMove ? 'w' : 'b';

    while(true)                                                     //  Follow capture chain to the end.
      {
        len = attackersOfSquare(target, team, &gs, buffer);
        if(len == 0)                                                //  No further captures.
          break;

        leastVal = SEE_SCORE_KING * 2;                              //  Find the least valuable captor.
        for(i = 0; i < len; i++)
          {
            val = SEE_lookup(gs.board[ buffer[i].from ]);
            if(val < leastVal)
              {
                chosenMove.from = buffer[i].from;
                chosenMove.to = buffer[i].to;
                chosenMove.promo = _NO_PROMO;
                leastVal = val;
              }
          }
        victimVal = SEE_lookup(capturingPiece);                     //  Look up value of the victim.
        gainsLen++;
        gains[gainsLen] = victimVal - gains[gainsLen - 1];

        capturingPiece = gs.board[chosenMove.from];                 //  Identify the capturing piece.
        makeMove(&chosenMove, &gs);                                 //  Make the capture.
        team = gs.whiteToMove ? 'w' : 'b';
      }

    if(gainsLen > 0)
      {
        for(j = gainsLen - 1; j >= 0; j--)
          {
            stopHere = -gains[j];                                   //  It is in side-to-move's interest to stop here.
            continueExchange = gains[j + 1];                        //  It is in side-to-move's interest to continue exchanging.
            gains[j] = (stopHere > continueExchange) ? stopHere : continueExchange;
          }
      }

    return gains[0];
  }

/* Look up a rough material score for each piece. */
signed int SEE_lookup(char piece)
  {
    if(piece == _WHITE_PAWN || piece == _BLACK_PAWN)
      return SEE_SCORE_PAWN;
    if(piece == _WHITE_KNIGHT || piece == _BLACK_KNIGHT)
      return SEE_SCORE_KNIGHT;
    if(piece == _WHITE_BISHOP || piece == _BLACK_BISHOP)
      return SEE_SCORE_BISHOP;
    if(piece == _WHITE_ROOK || piece == _BLACK_ROOK)
      return SEE_SCORE_ROOK;
    if(piece == _WHITE_CARDINAL || piece == _BLACK_CARDINAL)
      return SEE_SCORE_CARDINAL;
    if(piece == _WHITE_MARSHAL || piece == _BLACK_MARSHAL)
      return SEE_SCORE_MARSHAL;
    if(piece == _WHITE_QUEEN || piece == _BLACK_QUEEN)
      return SEE_SCORE_QUEEN;
    if(piece == _WHITE_KING || piece == _BLACK_KING)
      return SEE_SCORE_KING;
    return 0;
  }
