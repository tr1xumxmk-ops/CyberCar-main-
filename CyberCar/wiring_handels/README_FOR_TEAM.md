# دليل الفريق — شرح كامل للكود
# Team Guide — Complete Code Walkthrough

هذا الملف يشرح كل جزء من الكود بالعربية والإنجليزية، مخصص لأعضاء الفريق
الجدد وللطلاب الذين يريدون فهم المشروع بعمق.

This file explains every part of the code in Arabic and English. Written
for new team members and students who want to understand the project
deeply.

---

## 1. البنية العامة / Overall Structure

### العربية

المشروع مقسم إلى ملفات منفصلة، كل ملف له مسؤولية واحدة واضحة:

- **`config.h`** — كل الثوابت والإعدادات، والأدوات المساعدة
- **`image_data.h`** — فقط بيانات الصورة، لا شيء آخر
- **`bmp_resource.h`** — رأس BMP الذي يحول البيانات الخام إلى صورة
- **`ai_module.h`** — الشبكة العصبية وخوارزمية التعلم
- **`captive_portal.h`** — خادم الويب والواجهة
- **`CyberCarPro_optimized.ino`** — الملف الرئيسي: يربط كل شيء

هذا التقسيم يمنع الأخطاء عند الترجمة ويجعل التعديل سهل.

### English

The project is split into single-responsibility files:

- **`config.h`** — All constants, settings, and helpers
- **`image_data.h`** — Only pixel data, nothing else
- **`bmp_resource.h`** — BMP header that turns raw data into a viewable image
- **`ai_module.h`** — Neural network and learning algorithm
- **`captive_portal.h`** — Web server and UI
- **`CyberCarPro_optimized.ino`** — Main file that wires everything together

This separation prevents compile errors and makes editing easy.

---

## 2. ملف الإعدادات / Configuration File (`config.h`)

### العربية

كل الأرقام المهمة في المشروع موجودة هنا. مثال:

- `SAFE_DISTANCE_CM = 25` — إذا كانت المسافة أقل، السيارة تتوقف
- `CRITICAL_DISTANCE_CM = 15` — إذا كانت أقل، خطر حقيقي
- `MANUAL_TIMEOUT_MS = 700` — إذا اختفت إشارة التحكم 700ms، توقف تلقائياً

**لماذا هذا مهم؟** لو أردنا تعديل سلوك السيارة، نعدل رقم واحد في مكان واحد
فقط، وكل الكود سيتكيف تلقائياً.

### English

All important numbers live here:

- `SAFE_DISTANCE_CM = 25` — Below this the car stops
- `CRITICAL_DISTANCE_CM = 15` — Real danger zone
- `MANUAL_TIMEOUT_MS = 700` — Auto-stop if control signal is lost for 700ms

**Why this matters:** To change behavior, edit one number in one place.
Every other part of the code adapts automatically.

---

## 3. الصورة / The Image (`image_data.h` + `bmp_resource.h`)

### العربية

عندك صورة 1408×768 بكسل. كل بكسل أبيض أو أسود = بت واحد. إذاً:

- العرض = 1408 ÷ 8 = 176 بايت لكل صف
- 176 × 768 = **135,168 بايت** — حجم البيانات

المشكلة: المتصفح لا يعرف كيف يعرض هذا، فهو يحتاج ملف BMP له رأس محدد.

`bmp_resource.h` يحتوي على 62 بايت فقط (الرأس). عند طلب الصورة،
الخادم يرسل الرأس أولاً، ثم 135,168 بايت من `image_data.h`، فتصبح صورة كاملة.

### English

The image is 1408×768 pixels. Each pixel is black or white = 1 bit. So:

- Width = 1408 ÷ 8 = 176 bytes per row
- 176 × 768 = **135,168 bytes** of pixel data

The browser can't display raw bits — it needs a BMP file with a header.

`bmp_resource.h` contains just 62 bytes (the header). When a browser
requests `/image`, the server sends the header, then the 135 KB of pixels
from `image_data.h`. The result is a valid BMP.

**Why chunks of 1 KB?** The ESP32 watchdog will reset the chip if a single
write blocks too long. We send 1 KB at a time and reset the watchdog after
each chunk.

---

## 4. الشبكة العصبية / The Neural Network (`ai_module.h` → `QuantizedMLP`)

### العربية

شبكة عصبية صغيرة 5 مدخلات ← 8 خلايا مخفية ← 3 مخرجات:

- المدخلات: المسافة الأمامية، اليمنى، اليسرى، سرعة الاقتراب، السرعة الحالية
- المخرجات: احتمال الأمام، احتمال اليسار، احتمال اليمين

نستخدم **أعداد صحيحة (int8)** بدلاً من الأعداد العشرية لأن:
1. ESP32 أبطأ في العمليات العشرية
2. الذاكرة محدودة

الطريقة: نضرب الأرقام ثم نقسم على 128 (تحويل من Q1.6 إلى int16).

**الأمان أولاً**: إذا كانت المسافة الأمامية أقل من 25 سم، نتجاوز الشبكة
بالكامل ونفرض دوراناً. الشبكة قد تخطئ، الأمان لا يخطئ.

### English

A small neural network: 5 inputs → 8 hidden → 3 outputs:

- Inputs: front, right, left, approach rate, current speed
- Outputs: probability of forward, left, right

We use **int8 weights** instead of floats because:
1. ESP32 is slow at floating-point math
2. Memory is limited

The trick: multiply by int8 weights, then shift right by 7 (converts Q1.6
to int16 fixed-point).

**Safety first**: If front distance < 25 cm, we bypass the network entirely
and force a turn. The network can be wrong; safety cannot.

---

## 5. التعلم المعزز / Reinforcement Learning (`ai_module.h` → `QuantizedDoubleQLearning`)

### العربية

هذه أهم قطعة في المشروع. النموذج يتعلم من التجربة.

**الفكرة**: نعطي "مكافأة" أو "عقوبة" لكل حركة. النموذج يتعلم أي حركة تعطي
أعلى مجموع مكافآت.

**المفاهيم الأساسية**:

- **الحالة** (`state`): 8 حالات = (4 مناطق للمسافة) × (2 اتجاه: ثابت أو يقترب)
- **الحركة** (`action`): 5 حركات = أمام، يسار، يمين، خلف، فرامل
- **المكافأة**: كل حركة لها مكافأة تُحسب بعد 180ms من التنفيذ

**Double Q-learning**: نحتفظ بجدولين Q بدلاً من واحد. نستخدم الأول لاختيار
الحركة، الثاني لتقييمها. هذا يقلل المبالغة في التقدير.

**Experience Replay**: نحتفظ بآخر 24 تجربة في حلقة. كل 3 تحديثات، نختار
تجربة عشوائية قديمة ونعيد التعلم منها. هذا يجعل التعلم أكثر استقراراً.

**Epsilon-greedy with decay**: النموذج يبدأ بـ 50% استكشاف (حركات عشوائية)
ثم ينقص 1% كل 50 تحديث حتى 5%. هذا يعني: يتعلم أولاً، ثم يستخدم ما تعلم.

**النافذة الزمنية**: نستخدم آخر 3 قراءات لحساب "سرعة الاقتراب". إذا اقتربنا
أكثر من 2 سم في القراءات الأخيرة، الحالة تتغير.

**الحفظ**: كل 20 ثانية، يُحفظ كل شيء في Flash (NVS). عند إعادة التشغيل،
يستأنف التعلم من حيث توقف.

**القياسات الحية**:
- `updates` — عدد مرات التعلم
- `epsilon` — نسبة الاستكشاف الحالية
- `best` — أعلى مجموع مكافآت في جولة كاملة
- `episodes` — عدد الجولات المنتهية

### English

This is the heart of the project. The model learns from experience.

**Core idea**: We give a reward or penalty for each movement. The model
learns which action maximizes total reward over time.

**Key concepts**:

- **State**: 8 states = (4 distance zones) × (2 trends: stable or approaching)
- **Action**: 5 actions = forward, left, right, reverse, brake
- **Reward**: Every action gives a reward, computed 180ms after execution

**Double Q-learning**: We keep two Q-tables. One selects the action, the
other evaluates it. This reduces over-estimation bias, a known problem in
standard Q-learning.

**Experience Replay**: Last 24 transitions are kept in a ring buffer.
Every 3 updates, we sample an old transition and learn from it again.
This stabilizes learning.

**Epsilon-greedy with decay**: The model starts at 50% exploration (random
actions) and decreases by 1% every 50 updates, down to 5%. Meaning: explore
first, then exploit what's learned.

**Temporal window**: We use the last 3 readings to compute "approach rate".
If we're getting closer by more than 2 cm per reading, the state changes.

**Persistence**: Every 20 seconds, everything is saved to NVS. On reboot,
learning resumes from where it stopped.

**Live telemetry**:
- `updates` — number of learning steps
- `epsilon` — current exploration percentage
- `best` — highest episode reward ever
- `episodes` — number of completed episodes

---

## 6. الحالات الثلاث / The Three Modes (`CarNavigator`)

### العربية

**يدوي (Manual)**:
- حركة السيارة تتبع الـ joystick فقط
- إذا توقف الـ joystick عن الإرسال 700ms، توقف تلقائياً
- إذا المسافة الأمامية < 25 سم أثناء التقدم، فرملة إجبارية
- لا يمكن