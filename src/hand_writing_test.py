import cv2
import mediapipe as mp
import math
import os
import json
import time

# ============================================================
# HAND WRITING LETTER RECOGNITION - VERSION 3
# ============================================================
# IMPORTANT CHANGE:
# Instead of comparing your handwriting with my fixed letters,
# this version lets YOU teach the program your own handwriting.
#
# CALIBRATION:
#   1. Press T to start training mode.
#   2. Draw R with your index finger.
#   3. Close the index finger to finish the drawing.
#   4. Press R on the keyboard to label that drawing as R.
#   5. Repeat 3 times for R.
#   6. Do the same for L U D F B.
#
# After training:
#   Press T to switch to TEST mode.
#   Draw a letter and close the index finger.
#   The program compares it with YOUR examples.
#
# White MediaPipe dots/lines remain visible.
# White writing remains visible.
#
# RECOGNIZED LETTERS ARE SENT TO THE RAYLIB CUBE.
# ============================================================

CAMERA_FRAME_PATH = r"D:\RubiksCubeGame\raylib-quickstart-main\src\camera_frame.bmp"
TRAINING_FILE = r"D:\RubiksCubeGame\raylib-quickstart-main\src\my_handwriting_templates.json"
HAND_COMMAND_PATH = r"D:\RubiksCubeGame\raylib-quickstart-main\src\hand_command.txt"
HAND_GESTURE_PATH = r"D:\RubiksCubeGame\raylib-quickstart-main\src\hand_gesture.txt"

LETTERS = ["R", "L", "U", "D", "F", "B"]

NUM_POINTS = 64
MIN_POINT_DISTANCE = 4
MIN_LETTER_POINTS = 20
MAX_STORED_POINTS = 10000

# Number of examples to collect for each letter.
EXAMPLES_PER_LETTER = 3

# Recognition tolerance.
MAX_ACCEPT_DISTANCE = 0.42

# ============================================================
# MediaPipe
# ============================================================

mp_hands = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils

hands = mp_hands.Hands(
    static_image_mode=False,
    max_num_hands=1,
    min_detection_confidence=0.55,
    min_tracking_confidence=0.55
)

# ============================================================
# Camera
# ============================================================

cap = cv2.VideoCapture(0)

if not cap.isOpened():
    print("ERROR: Could not open camera.")
    raise SystemExit

cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

# ============================================================
# State
# ============================================================

mode = "TEST"

writing = False
last_tip = None

writing_points = []
current_stroke = []

detected_letter = "NONE"
confidence = 0.0

pending_sample = None
pending_message = ""

templates = {
    letter: []
    for letter in LETTERS
}

# ============================================================
# Geometry
# ============================================================

def distance(a, b):
    return math.hypot(
        a[0] - b[0],
        a[1] - b[1]
    )


def path_length(points):

    total = 0.0

    for i in range(1, len(points)):
        total += distance(
            points[i - 1],
            points[i]
        )

    return total


def resample(points, n=NUM_POINTS):

    if len(points) < 2:
        return points[:]

    total = path_length(points)

    if total <= 0:
        return [points[0]] * n

    interval = total / (n - 1)

    result = [points[0]]

    accumulated = 0.0
    previous = points[0]

    i = 1

    while i < len(points):

        current = points[i]

        segment = distance(
            previous,
            current
        )

        if segment == 0:
            i += 1
            continue

        if accumulated + segment >= interval:

            ratio = (
                (interval - accumulated)
                / segment
            )

            q = (
                previous[0]
                + ratio * (current[0] - previous[0]),

                previous[1]
                + ratio * (current[1] - previous[1])
            )

            result.append(q)

            previous = q
            accumulated = 0.0

        else:

            accumulated += segment
            previous = current
            i += 1

    while len(result) < n:
        result.append(result[-1])

    return result[:n]


def normalize(points):

    if not points:
        return []

    min_x = min(p[0] for p in points)
    max_x = max(p[0] for p in points)

    min_y = min(p[1] for p in points)
    max_y = max(p[1] for p in points)

    width = max_x - min_x
    height = max_y - min_y

    size = max(width, height)

    if size < 1:
        size = 1

    center_x = (min_x + max_x) / 2.0
    center_y = (min_y + max_y) / 2.0

    # Center the letter around 0.
    result = []

    for x, y in points:

        result.append((
            (x - center_x) / size,
            (y - center_y) / size
        ))

    return result


def prepare(points):

    return normalize(
        resample(
            points,
            NUM_POINTS
        )
    )


def dtw_distance(a, b):

    n = len(a)
    m = len(b)

    dp = [
        [999999.0] * (m + 1)
        for _ in range(n + 1)
    ]

    dp[0][0] = 0.0

    for i in range(1, n + 1):

        for j in range(1, m + 1):

            cost = distance(
                a[i - 1],
                b[j - 1]
            )

            dp[i][j] = cost + min(
                dp[i - 1][j],
                dp[i][j - 1],
                dp[i - 1][j - 1]
            )

    return dp[n][m] / (n + m)


# ============================================================
# Save / load personal templates
# ============================================================

def save_templates():

    try:

        data = {
            letter: templates[letter]
            for letter in LETTERS
        }

        with open(
            TRAINING_FILE,
            "w",
            encoding="utf-8"
        ) as f:

            json.dump(
                data,
                f
            )

        print("Training saved.")

    except Exception as e:

        print(
            "Could not save training:",
            e
        )


def load_templates():

    global templates

    try:

        if not os.path.exists(
            TRAINING_FILE
        ):
            return

        with open(
            TRAINING_FILE,
            "r",
            encoding="utf-8"
        ) as f:

            data = json.load(f)

        for letter in LETTERS:

            if letter in data:

                templates[letter] = data[letter]

        print(
            "Loaded handwriting templates."
        )

    except Exception as e:

        print(
            "Could not load templates:",
            e
        )


load_templates()

# Remove any old command left by a previous run.
try:
    if os.path.exists(HAND_COMMAND_PATH):
        os.remove(HAND_COMMAND_PATH)
except Exception:
    pass


# ============================================================
# Recognition
# ============================================================

def recognize(points):

    if len(points) < MIN_LETTER_POINTS:
        return "?", 0.0

    user = prepare(points)

    candidates = []

    for letter in LETTERS:

        if not templates[letter]:
            continue

        for example in templates[letter]:

            example_points = [
                tuple(p)
                for p in example
            ]

            d = dtw_distance(
                user,
                example_points
            )

            candidates.append(
                (d, letter)
            )

    if not candidates:
        return "TRAIN", 0.0

    candidates.sort(
        key=lambda x: x[0]
    )

    best_distance = candidates[0][0]
    best_letter = candidates[0][1]

    # Convert distance to a simple score.
    score = max(
        0.0,
        1.0 - best_distance / 0.50
    )

    if best_distance > MAX_ACCEPT_DISTANCE:
        return "?", score

    return best_letter, score


# ============================================================
# Finger posture
# ============================================================

def finger_closed(lm, tip, pip):

    return (
        lm[tip].y
        > lm[pip].y - 0.03
    )


def index_open(lm):

    return (
        lm[8].y
        < lm[6].y - 0.015
    )


def writing_hand(lm):

    return (
        index_open(lm)
        and finger_closed(lm, 12, 10)
        and finger_closed(lm, 16, 14)
        and finger_closed(lm, 20, 18)
    )


# ============================================================
# Add fingertip
# ============================================================

def add_point(point):

    global writing_points
    global current_stroke

    if not current_stroke:

        current_stroke.append(point)
        writing_points.append(point)

        return

    if distance(
        current_stroke[-1],
        point
    ) >= MIN_POINT_DISTANCE:

        current_stroke.append(point)
        writing_points.append(point)

    if len(writing_points) > MAX_STORED_POINTS:

        writing_points = writing_points[
            -MAX_STORED_POINTS:
        ]


# ============================================================
# Send one recognized letter to the C/raylib game
# ============================================================

def send_command(letter):

    if letter not in LETTERS:
        return

    temp_path = HAND_COMMAND_PATH + ".tmp"

    try:

        with open(
            temp_path,
            "w",
            encoding="utf-8"
        ) as f:
            f.write(letter + "\n")

        os.replace(
            temp_path,
            HAND_COMMAND_PATH
        )

        try:
            gesture_temp = HAND_GESTURE_PATH + ".tmp"
            with open(gesture_temp, "w", encoding="utf-8") as f:
                f.write(letter + "\n")
            os.replace(gesture_temp, HAND_GESTURE_PATH)
        except Exception:
            pass

        print("CUBE COMMAND:", letter)

    except Exception as e:

        print("Could not send cube command:", e)


# ============================================================
# Finish current drawing
# ============================================================

def finish_letter():

    global current_stroke
    global pending_sample
    global detected_letter
    global confidence

    if len(current_stroke) < MIN_LETTER_POINTS:

        current_stroke = []
        return

    if mode == "TRAIN":

        pending_sample = prepare(
            current_stroke
        )

        detected_letter = "LABEL?"
        confidence = 0.0

        print()
        print(
            "Training sample ready."
        )
        print(
            "Press R, L, U, D, F or B"
        )
        print(
            "to label this drawing."
        )

    else:

        letter, score = recognize(
            current_stroke
        )

        detected_letter = letter
        confidence = score

        print(
            "Detected:",
            letter,
            "| score:",
            round(score, 2)
        )

        if letter in LETTERS:
            send_command(letter)

    current_stroke = []


# ============================================================
# Draw permanent white trail
# ============================================================

def draw_writing(frame):

    for i in range(
        1,
        len(writing_points)
    ):

        p1 = writing_points[i - 1]
        p2 = writing_points[i]

        if distance(p1, p2) < 100:

            cv2.line(
                frame,
                p1,
                p2,
                (255, 255, 255),
                3,
                cv2.LINE_AA
            )


# ============================================================
# Save camera frame for raylib
# ============================================================

def save_camera_frame(frame):

    temp_path = (
        CAMERA_FRAME_PATH
        + ".tmp.bmp"
    )

    try:

        cv2.imwrite(
            temp_path,
            frame
        )

        os.replace(
            temp_path,
            CAMERA_FRAME_PATH
        )

    except Exception:
        pass


# ============================================================
# Training label
# ============================================================

def label_pending(letter):

    global pending_sample
    global detected_letter
    global pending_message

    if pending_sample is None:
        return

    if letter not in LETTERS:
        return

    if len(templates[letter]) >= EXAMPLES_PER_LETTER:

        pending_message = (
            letter
            + " already has "
            + str(EXAMPLES_PER_LETTER)
            + " samples."
        )

        pending_sample = None
        return

    templates[letter].append(
        pending_sample
    )

    count = len(
        templates[letter]
    )

    save_templates()

    # --------------------------------------------------------
    # CLEAR THE WHITE WRITING AFTER SAVING THE SAMPLE
    # --------------------------------------------------------
    # The trained sample is already stored in memory/file, so
    # we can safely clear the visible drawing and prepare for
    # the next letter.
    writing_points.clear()
    current_stroke.clear()

    pending_sample = None

    detected_letter = letter

    pending_message = (
        "Saved "
        + letter
        + " sample "
        + str(count)
        + "/"
        + str(EXAMPLES_PER_LETTER)
        + " - BOARD CLEARED"
    )

    print(
        pending_message
    )


# ============================================================
# Main
# ============================================================

print()
print("================================================")
print("      PERSONAL HANDWRITING RECOGNITION V3")
print("================================================")
print()
print("Supported letters: R L U D F B")
print()
print("C = CLEAR")
print("Q = QUIT")
print()
print("TEST MODE: trained handwriting controls the cube")
print("Draw R/L/U/D/F/B -> close index finger -> cube receives one move")
print()
print("================================================")
print()


while True:

    ok, frame = cap.read()

    if not ok:
        continue

    frame = cv2.flip(
        frame,
        1
    )

    rgb = cv2.cvtColor(
        frame,
        cv2.COLOR_BGR2RGB
    )

    result = hands.process(rgb)

    hand_found = False

    # --------------------------------------------------------
    # Hand
    # --------------------------------------------------------

    if result.multi_hand_landmarks:

        hand_found = True

        hand = result.multi_hand_landmarks[0]
        lm = hand.landmark

        # Keep MediaPipe dots and lines.
        mp_drawing.draw_landmarks(
            frame,
            hand,
            mp_hands.HAND_CONNECTIONS,

            mp_drawing.DrawingSpec(
                color=(255, 255, 255),
                thickness=2,
                circle_radius=3
            ),

            mp_drawing.DrawingSpec(
                color=(255, 255, 255),
                thickness=2
            )
        )

        h, w = frame.shape[:2]

        tip = (
            int(lm[8].x * w),
            int(lm[8].y * h)
        )

        # Highlight index fingertip.
        cv2.circle(
            frame,
            tip,
            7,
            (255, 255, 255),
            -1,
            cv2.LINE_AA
        )

        # ----------------------------------------------------
        # Writing
        # ----------------------------------------------------

        if writing_hand(lm):

            if not writing:

                current_stroke = []
                last_tip = tip
                writing = True

            if last_tip is not None:

                if distance(
                    last_tip,
                    tip
                ) < 120:

                    add_point(tip)

            last_tip = tip

        else:

            # Closing the index finishes the letter.
            if writing:

                finish_letter()

                last_tip = None
                writing = False

    else:

        if writing:

            finish_letter()

            last_tip = None
            writing = False

    # --------------------------------------------------------
    # Permanent writing
    # --------------------------------------------------------

    draw_writing(frame)

    # --------------------------------------------------------
    # UI
    # --------------------------------------------------------

    cv2.rectangle(
        frame,
        (8, 8),
        (430, 190),
        (255, 255, 255),
        2
    )

    cv2.putText(
        frame,
        "HAND WRITING CUBE CONTROL",
        (20, 37),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.65,
        (255, 255, 255),
        2,
        cv2.LINE_AA
    )

    cv2.putText(
        frame,
        "MODE: CUBE CONTROL",
        (20, 65),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        1,
        cv2.LINE_AA
    )

    if writing:

        status = "WRITING..."

    elif hand_found:

        status = "HAND FOUND"

    else:

        status = "SHOW ONE HAND"

    cv2.putText(
        frame,
        status,
        (20, 92),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.48,
        (255, 255, 255),
        1,
        cv2.LINE_AA
    )

    cv2.putText(
        frame,
        "DETECTED: " + detected_letter,
        (20, 122),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.65,
        (255, 255, 255),
        2,
        cv2.LINE_AA
    )

    cv2.putText(
        frame,
        "SCORE: " + str(round(confidence, 2)),
        (20, 148),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.43,
        (255, 255, 255),
        1,
        cv2.LINE_AA
    )

    # Training progress.
    progress = "TRAIN: "

    for letter in LETTERS:

        progress += (
            letter
            + ":"
            + str(len(templates[letter]))
            + "  "
        )

    cv2.putText(
        frame,
        progress,
        (20, 174),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.36,
        (255, 255, 255),
        1,
        cv2.LINE_AA
    )

    if pending_message:

        cv2.putText(
            frame,
            pending_message,
            (440, 35),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.50,
            (255, 255, 255),
            2,
            cv2.LINE_AA
        )

    # --------------------------------------------------------
    # Save for raylib
    # --------------------------------------------------------

    save_camera_frame(frame)

    # --------------------------------------------------------
    # Preview
    # --------------------------------------------------------

    cv2.imshow(
        "Personal Handwriting Test",
        frame
    )

    key = cv2.waitKey(1) & 0xFF

    # --------------------------------------------------------
    # Clear
    # --------------------------------------------------------

    if key == ord("c"):

        writing_points.clear()
        current_stroke.clear()

        detected_letter = "NONE"
        confidence = 0.0

        pending_sample = None
        pending_message = ""

        last_tip = None
        writing = False

    # --------------------------------------------------------
    # Quit
    # --------------------------------------------------------

    elif key == ord("q") or key == 27:

        break


# ============================================================
# Cleanup
# ============================================================

save_templates()

cap.release()
hands.close()
cv2.destroyAllWindows()

print()
print("Personal handwriting test stopped.")