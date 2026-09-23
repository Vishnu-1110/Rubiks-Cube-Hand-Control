#include <stdio.h>
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

// ============================================================
// LIVE OPENCV CAMERA PREVIEW
// ============================================================
// Python writes the latest webcam frame to this BMP file.
// The C/raylib program only reads the JPG; no Windows.h,
// Winsock, SOCKET, or other Windows networking code is needed.

#define CAMERA_BOX_X 6
#define CAMERA_BOX_Y 518
#define CAMERA_BOX_W 141
#define CAMERA_BOX_H 117

#define CAMERA_FRAME_PATH "D:\\RubiksCubeGame\\raylib-quickstart-main\\src\\camera_frame.bmp"
#define CAMERA_SCRIPT_PATH "D:\\RubiksCubeGame\\raylib-quickstart-main\\src\\hand_writing_test.py"
#define HAND_GESTURE_PATH "D:\\RubiksCubeGame\\raylib-quickstart-main\\src\\hand_gesture.txt"
#define HAND_COMMAND_PATH "D:\\RubiksCubeGame\\raylib-quickstart-main\\src\\hand_command.txt"

static Texture2D cameraTexture = { 0 };
static bool cameraTextureLoaded = false;
static float cameraReloadTimer = 0.0f;

// ============================================================
// HAND GESTURE TEST MODE
// ============================================================
// Python/MediaPipe writes ONE confirmed gesture to this file.
// C only displays the detected gesture during this test phase.
// The cube is NOT moved by hand gestures yet.

static bool handTestMode = true;
static char handGesture[128] = "WAITING FOR HAND...";
static float handGestureDisplayTimer = 0.0f;

// ============================================================
// WINDOW
// ============================================================

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 650

// ============================================================
// CUBE SETTINGS
// ============================================================

#define CUBIE_SIZE 1.0f
#define GAP 0.08f
#define STICKER_SIZE 0.82f
#define STICKER_OFFSET 0.525f

#define MOVE_TIME 0.22f

// ============================================================
// FACE INDEX
// ============================================================

enum
{
    FACE_RIGHT = 0,
    FACE_LEFT,
    FACE_TOP,
    FACE_BOTTOM,
    FACE_FRONT,
    FACE_BACK
};

// ============================================================
// CUBIE
// ============================================================

typedef struct
{
    int x;
    int y;
    int z;

    Color face[6];

} Cubie;

// ============================================================
// INTEGER VECTOR
// ============================================================

typedef struct
{
    int x;
    int y;
    int z;

} IntVector;

// ============================================================
// MOVE ANIMATION
// ============================================================

typedef struct
{
    bool active;

    char move;
    char axis;

    int layer;
    int direction;

    float timer;

} MoveAnimation;

// ============================================================
// MOVE HISTORY
// ============================================================

#define HISTORY_MAX 200

typedef struct
{
    Cubie cubies[27];

} CubeState;

CubeState undoStack[HISTORY_MAX];
CubeState redoStack[HISTORY_MAX];

int undoCount = 0;
int redoCount = 0;

// State saved when an animated move starts.
// It becomes an undo state when that move finishes.
CubeState pendingMoveBefore;


// ============================================================
// PHASE 1 - SCRAMBLE / SOLVED / RESET
// ============================================================

#define SCRAMBLE_LENGTH 20

char scrambleMoves[SCRAMBLE_LENGTH];
bool scramblePrime[SCRAMBLE_LENGTH];
bool scrambleDouble[SCRAMBLE_LENGTH];
int scrambleIndex = 0;
bool scrambleActive = false;
char scrambleText[256] = "";

bool showSolvedMessage = false;
float solvedMessageTimer = 0.0f;

float solvedAnimationAngle = 0.0f;
float solvedAnimationTimer = 0.0f;
bool solvedAnimationActive = false;

// ============================================================
// PHASE 2 - SPEEDCUBING TIMER / STATISTICS
// ============================================================

#define MAX_SOLVES 1000

double solveTimes[MAX_SOLVES];
int totalSolves = 0;

double solveTimer = 0.0;
bool timerRunning = false;
int currentMoveCount = 0;

double bestSingle = -1.0;
double bestAverage5 = -1.0;
double bestAverage12 = -1.0;

// Font used for the speedcubing statistics and solved popup.
Font statsFont = { 0 };


// ============================================================
// PHASE 3 - REAL AUTO SOLVER (KOCIEMBA)
// ============================================================

#define SOLVER_MAX_MOVES 200

// A solver move is one standard face turn.
// R   = quarter turn
// R'  = prime turn
// R2  = double turn
//
typedef struct
{
    char move;
    bool prime;
    bool doubleMove;
} SolverMove;

SolverMove solverMoves[SOLVER_MAX_MOVES];
int solverLength = 0;
int solverIndex = 0;
bool solverActive = false;
bool solverDoublePending = false;

char solverSolutionText[1024] = "";
char solverStatus[256] = "";

// Forward declaration used by the solver start function.
bool IsCubeSolved(void);


// ============================================================
// CUBE
// ============================================================

Cubie cube[27];

// ============================================================
// COLORS
// ============================================================

Color GetFaceColor(int face)
{
    switch (face)
    {
    case FACE_RIGHT:
        return BLUE;

    case FACE_LEFT:
        return GREEN;

    case FACE_TOP:
        return YELLOW;

    case FACE_BOTTOM:
        return WHITE;

    case FACE_FRONT:
        return RED;

    case FACE_BACK:
        return ORANGE;
    }

    return BLACK;
}

// ============================================================
// FACE NORMAL
// ============================================================

IntVector FaceNormal(int face)
{
    switch (face)
    {
    case FACE_RIGHT:
        return (IntVector) { 1, 0, 0 };

    case FACE_LEFT:
        return (IntVector) { -1, 0, 0 };

    case FACE_TOP:
        return (IntVector) { 0, 1, 0 };

    case FACE_BOTTOM:
        return (IntVector) { 0, -1, 0 };

    case FACE_FRONT:
        return (IntVector) { 0, 0, 1 };

    case FACE_BACK:
        return (IntVector) { 0, 0, -1 };
    }

    return (IntVector) { 0, 0, 0 };
}

// ============================================================
// NORMAL TO FACE
// ============================================================

int NormalToFace(IntVector n)
{
    if (n.x == 1)
        return FACE_RIGHT;

    if (n.x == -1)
        return FACE_LEFT;

    if (n.y == 1)
        return FACE_TOP;

    if (n.y == -1)
        return FACE_BOTTOM;

    if (n.z == 1)
        return FACE_FRONT;

    if (n.z == -1)
        return FACE_BACK;

    return -1;
}

// ============================================================
// ROTATE INTEGER VECTOR X
// ============================================================

IntVector RotateX(IntVector v, int dir)
{
    IntVector r;

    if (dir > 0)
    {
        r.x = v.x;
        r.y = -v.z;
        r.z = v.y;
    }
    else
    {
        r.x = v.x;
        r.y = v.z;
        r.z = -v.y;
    }

    return r;
}

// ============================================================
// ROTATE INTEGER VECTOR Y
// ============================================================

IntVector RotateY(IntVector v, int dir)
{
    IntVector r;

    if (dir > 0)
    {
        r.x = v.z;
        r.y = v.y;
        r.z = -v.x;
    }
    else
    {
        r.x = -v.z;
        r.y = v.y;
        r.z = v.x;
    }

    return r;
}

// ============================================================
// ROTATE INTEGER VECTOR Z
// ============================================================

IntVector RotateZ(IntVector v, int dir)
{
    IntVector r;

    if (dir > 0)
    {
        r.x = -v.y;
        r.y = v.x;
        r.z = v.z;
    }
    else
    {
        r.x = v.y;
        r.y = -v.x;
        r.z = v.z;
    }

    return r;
}

// ============================================================
// INITIALIZE CUBE
// ============================================================

void InitializeCube(void)
{
    int index = 0;

    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            for (int z = -1; z <= 1; z++)
            {
                cube[index].x = x;
                cube[index].y = y;
                cube[index].z = z;

                for (int i = 0; i < 6; i++)
                {
                    cube[index].face[i] = BLANK;
                }

                if (x == 1)
                    cube[index].face[FACE_RIGHT] = BLUE;

                if (x == -1)
                    cube[index].face[FACE_LEFT] = GREEN;

                if (y == 1)
                    cube[index].face[FACE_TOP] = YELLOW;

                if (y == -1)
                    cube[index].face[FACE_BOTTOM] = WHITE;

                if (z == 1)
                    cube[index].face[FACE_FRONT] = RED;

                if (z == -1)
                    cube[index].face[FACE_BACK] = ORANGE;

                index++;
            }
        }
    }
}

// ============================================================
// ROTATE ONE CUBIE
// ============================================================

void RotateCubie(Cubie* c, char axis, int dir)
{
    IntVector position =
    {
        c->x,
        c->y,
        c->z
    };

    IntVector newPosition;

    if (axis == 'X')
        newPosition = RotateX(position, dir);

    else if (axis == 'Y')
        newPosition = RotateY(position, dir);

    else
        newPosition = RotateZ(position, dir);

    c->x = newPosition.x;
    c->y = newPosition.y;
    c->z = newPosition.z;

    Color oldFaces[6];

    for (int i = 0; i < 6; i++)
        oldFaces[i] = BLANK;

    for (int oldFace = 0; oldFace < 6; oldFace++)
    {
        if (c->face[oldFace].a == 0)
            continue;

        IntVector normal = FaceNormal(oldFace);

        IntVector newNormal;

        if (axis == 'X')
            newNormal = RotateX(normal, dir);

        else if (axis == 'Y')
            newNormal = RotateY(normal, dir);

        else
            newNormal = RotateZ(normal, dir);

        int newFace = NormalToFace(newNormal);

        if (newFace >= 0)
            oldFaces[newFace] = c->face[oldFace];
    }

    for (int i = 0; i < 6; i++)
        c->face[i] = oldFaces[i];
}

// ============================================================
// GET MOVE INFORMATION
// ============================================================

bool GetMoveInfo(
    char move,
    bool prime,
    char* axis,
    int* layer,
    int* direction
)
{
    switch (move)
    {
    case 'R':
        *axis = 'X';
        *layer = 1;
        *direction = prime ? -1 : 1;
        return true;

    case 'L':
        *axis = 'X';
        *layer = -1;
        *direction = prime ? 1 : -1;
        return true;

    case 'U':
        *axis = 'Y';
        *layer = 1;
        *direction = prime ? -1 : 1;
        return true;

    case 'D':
        *axis = 'Y';
        *layer = -1;
        *direction = prime ? 1 : -1;
        return true;

    case 'F':
        *axis = 'Z';
        *layer = 1;
        *direction = prime ? -1 : 1;
        return true;

    case 'B':
        *axis = 'Z';
        *layer = -1;
        *direction = prime ? 1 : -1;
        return true;

        // X is handled as the middle slice. The exact rotation
        // axis/direction for the current camera view is calculated
        // by GetMiddleMove() before the move starts.
    case 'X':
        *axis = 'X';
        *layer = 0;
        *direction = prime ? -1 : 1;
        return true;

        // Z is also a middle slice. Its camera-relative direction
        // is calculated by GetMiddleMove().
    case 'Z':
        *axis = 'Z';
        *layer = 0;
        *direction = prime ? -1 : 1;
        return true;
    }

    return false;
}

// ============================================================
// COPY CUBE STATE
// ============================================================

void SaveCubeState(CubeState* state)
{
    for (int i = 0; i < 27; i++)
        state->cubies[i] = cube[i];
}

void RestoreCubeState(const CubeState* state)
{
    for (int i = 0; i < 27; i++)
        cube[i] = state->cubies[i];
}

// ============================================================
// PUSH UNDO STATE
// ============================================================

void PushUndoState(const CubeState* state)
{
    if (undoCount >= HISTORY_MAX)
    {
        for (int i = 1; i < HISTORY_MAX; i++)
            undoStack[i - 1] = undoStack[i];

        undoCount = HISTORY_MAX - 1;
    }

    undoStack[undoCount] = *state;
    undoCount++;

    // A new move creates a new branch, so old redo moves
    // are no longer valid.
    redoCount = 0;
}

// ============================================================
// UNDO
// ============================================================

void UndoMove(void)
{
    if (undoCount <= 0)
        return;

    // Save the current state so Ctrl+Y can restore it.
    if (redoCount < HISTORY_MAX)
    {
        redoStack[redoCount] = (CubeState){ 0 };
        SaveCubeState(&redoStack[redoCount]);
        redoCount++;
    }

    undoCount--;

    RestoreCubeState(
        &undoStack[undoCount]
    );
}

// ============================================================
// REDO
// ============================================================

void RedoMove(void)
{
    if (redoCount <= 0)
        return;

    // Save the current state so another Ctrl+Z can return to it.
    if (undoCount < HISTORY_MAX)
    {
        undoStack[undoCount] = (CubeState){ 0 };
        SaveCubeState(&undoStack[undoCount]);
        undoCount++;
    }

    redoCount--;

    RestoreCubeState(
        &redoStack[redoCount]
    );
}

// ============================================================
// FINISH MOVE
// ============================================================

void FinishMove(MoveAnimation* move)
{
    for (int i = 0; i < 27; i++)
    {
        bool rotate = false;

        if (move->axis == 'X' &&
            cube[i].x == move->layer)
        {
            rotate = true;
        }

        if (move->axis == 'Y' &&
            cube[i].y == move->layer)
        {
            rotate = true;
        }

        if (move->axis == 'Z' &&
            cube[i].z == move->layer)
        {
            rotate = true;
        }

        if (rotate)
        {
            RotateCubie(
                &cube[i],
                move->axis,
                move->direction
            );
        }
    }

    move->active = false;
    move->timer = 0.0f;

    // Normal player moves are stored in undo history.
    // Scramble moves are not stored there.
    if (!scrambleActive && !solverActive)
    {
        PushUndoState(&pendingMoveBefore);
    }
}

// ============================================================
// START MOVE
// ============================================================

void StartMove(
    MoveAnimation* move,
    char face,
    bool prime
)
{
    if (move->active)
        return;

    char axis;
    int layer;
    int direction;

    if (!GetMoveInfo(
        face,
        prime,
        &axis,
        &layer,
        &direction))
    {
        return;
    }

    move->active = true;

    move->move = face;
    move->axis = axis;
    move->layer = layer;
    move->direction = direction;

    move->timer = 0.0f;
}

// ============================================================
// PHASE 2 FUNCTION DECLARATION
// ============================================================

void StartSpeedcubingTimer(MoveAnimation* move);

// ============================================================
// START MOVE WITH HISTORY
// ============================================================

void StartMoveWithHistory(
    MoveAnimation* move,
    char face,
    bool prime
)
{
    if (move->active)
        return;

    SaveCubeState(&pendingMoveBefore);

    StartMove(
        move,
        face,
        prime
    );

    StartSpeedcubingTimer(move);
}

// ============================================================
// START CUSTOM SLICE MOVE WITH HISTORY
// ============================================================

void StartCustomMoveWithHistory(
    MoveAnimation* move,
    char moveName,
    char axis,
    int layer,
    int direction
)
{
    if (move->active)
        return;

    SaveCubeState(&pendingMoveBefore);

    move->active = true;
    move->move = moveName;
    move->axis = axis;
    move->layer = layer;
    move->direction = direction;
    move->timer = 0.0f;

    StartSpeedcubingTimer(move);
}

// ============================================================
// CHECK IF CUBIE IS MOVING
// ============================================================

bool IsCubieMoving(
    Cubie* c,
    MoveAnimation* move
)
{
    if (!move->active)
        return false;

    if (move->axis == 'X' &&
        c->x == move->layer)
        return true;

    if (move->axis == 'Y' &&
        c->y == move->layer)
        return true;

    if (move->axis == 'Z' &&
        c->z == move->layer)
        return true;

    return false;
}

// ============================================================
// DRAW STICKER
// ============================================================

void DrawRoundedSticker(
    Vector3 position,
    int face,
    Color color
)
{
    if (color.a == 0)
        return;

    float s = STICKER_SIZE;
    float o = STICKER_OFFSET;

    // Corner radius gives the stickers the rounded look
    // of a real modern speed cube.
    float radius = 0.12f;

    float inner = s - (radius * 2.0f);
    float thickness = 0.045f;

    Vector3 center = position;

    // Draw the two rectangular portions of the rounded
    // sticker, then four small cylinders for the corners.
    if (face == FACE_RIGHT || face == FACE_LEFT)
    {
        center.x +=
            (face == FACE_RIGHT) ? o : -o;

        DrawCube(
            center,
            thickness,
            inner,
            s,
            color
        );

        DrawCube(
            center,
            thickness,
            s,
            inner,
            color
        );

        float x = center.x;
        float y0 = center.y - s * 0.5f + radius;
        float y1 = center.y + s * 0.5f - radius;
        float z0 = center.z - s * 0.5f + radius;
        float z1 = center.z + s * 0.5f - radius;

        Vector3 corners[4] =
        {
            { x, y0, z0 },
            { x, y0, z1 },
            { x, y1, z0 },
            { x, y1, z1 }
        };

        for (int i = 0; i < 4; i++)
        {
            rlPushMatrix();

            rlTranslatef(
                corners[i].x,
                corners[i].y,
                corners[i].z
            );

            // Cylinder's default axis is Y.
            // Rotate Y -> X for a side sticker.
            rlRotatef(
                90.0f,
                0.0f,
                0.0f,
                1.0f
            );

            DrawCylinder(
                (Vector3) {
                0.0f, 0.0f, 0.0f
            },
                radius,
                radius,
                thickness,
                24,
                color
            );

            rlPopMatrix();
        }
    }
    else if (face == FACE_TOP || face == FACE_BOTTOM)
    {
        center.y +=
            (face == FACE_TOP) ? o : -o;

        DrawCube(
            center,
            inner,
            thickness,
            s,
            color
        );

        DrawCube(
            center,
            s,
            thickness,
            inner,
            color
        );

        float y = center.y;
        float x0 = center.x - s * 0.5f + radius;
        float x1 = center.x + s * 0.5f - radius;
        float z0 = center.z - s * 0.5f + radius;
        float z1 = center.z + s * 0.5f - radius;

        Vector3 corners[4] =
        {
            { x0, y, z0 },
            { x0, y, z1 },
            { x1, y, z0 },
            { x1, y, z1 }
        };

        for (int i = 0; i < 4; i++)
        {
            DrawCylinder(
                corners[i],
                radius,
                radius,
                thickness,
                24,
                color
            );
        }
    }
    else
    {
        center.z +=
            (face == FACE_FRONT) ? o : -o;

        DrawCube(
            center,
            inner,
            s,
            thickness,
            color
        );

        DrawCube(
            center,
            s,
            inner,
            thickness,
            color
        );

        float z = center.z;
        float x0 = center.x - s * 0.5f + radius;
        float x1 = center.x + s * 0.5f - radius;
        float y0 = center.y - s * 0.5f + radius;
        float y1 = center.y + s * 0.5f - radius;

        Vector3 corners[4] =
        {
            { x0, y0, z },
            { x0, y1, z },
            { x1, y0, z },
            { x1, y1, z }
        };

        for (int i = 0; i < 4; i++)
        {
            rlPushMatrix();

            rlTranslatef(
                corners[i].x,
                corners[i].y,
                corners[i].z
            );

            // Cylinder's default axis is Y.
            // Rotate Y -> Z for a front/back sticker.
            rlRotatef(