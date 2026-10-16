# دليل الاختبار — MultiCS r82a (2026-10-14)

دليل شامل لاختبارات المشروع: الوحدات، التكامل، soak، memcheck. كل اختبار موثَّق بالهدف، الأمر، والنتيجة المتوقعة.

---

## 1. نظرة عامة

| النوع | العدد | المدة | الأمر |
|---|---|---|---|
| **الوحدات** | 1643 فحص، 42 وحدة | ~5 ثوانٍ | `make -C make-x64 test` |
| **التكامل** | 530 سيناريو | ~5 دقائق | `make -C tests all` |
| **Soak** | 1200 ثانية | 20 دقيقة | `cd tests && bash soak.sh` |
| **Memcheck** | وحدات + خادم | ~10 دقائق | `cd make-x64 && bash memcheck.sh` |

---

## 2. الوحدات (`make test`)

### 2.1 الهدف

اختبار كل طبقة نقية (header-only) وكل دالة مستقلة بمعزل عن الخادم.

### 2.2 الأمر

```bash
cd make-x64
make test
```

### 2.3 النتيجة المتوقعة

```
== 25/25 passed, 0 failed ==
== 19/19 passed, 0 failed ==
...
(42 وحدة)
```

**الإجمالي:** 1643 فحص في 42 وحدة، كلها خضراء.

### 2.4 الوحدات المهمة

| الوحدة | الوظيفة | الفحوصات |
|---|---|---|
| `test_cexvis` | R20: cacheex visibility layer | 19 |
| `test_cexwire` | R17: oscam wire format | 64 |
| `test_cafamily` | R16: CAID family tables | 37 |
| `test_chanhealth` | R15: channel health layer | — |
| `test_ptguard` | R14: pollution guard layer | — |
| `test_cwlate` | R13: late-truth layer | — |
| `test_parser` | config parser | 49 |
| `test_ledger` | agreement ledger | 60 |
| `test_cwtime` | CW timing | 68 |
| `test_cwplaus` | CW plausibility | 56 |
| `test_cwentropy` | CW entropy checks | 50 |

### 2.5 استكشاف الأخطاء

**`make test` يفشل:**
```bash
rm -rf x64
make test
```

**وحدة معينة تفشل:**
```bash
./x64/test_cexvis
# سترى الفشل بالتفصيل
```

---

## 3. التكامل (`make -C tests all`)

### 3.1 الهدف

اختبار الخادم الحقيقي تحت سيناريوهات حية (عملاء، نظراء، خوادم).

### 3.2 الأمر

```bash
# ابنِ الخادم أولاً
cd make-x64
make
cd ../tests

# شغّل الاختبارات
make all
```

### 3.3 النتيجة المتوقعة

```
[ ok ] scenario 1: ...
[ ok ] scenario 2: ...
...
530 ok, 0 FAIL
```

### 3.4 الأهداف الفردية

```bash
make r20    # R20: cacheex visibility (12 سيناريو)
make r19    # R19: cccam client platform (10 سيناريوهات)
make r18    # R18: MAXHOP_LG (10 سيناريوهات)
make r17    # R17: oscam compat (22 سيناريو)
make cf     # R16: CAID family (8 سيناريوهات)
make ch     # R15: channel health (4 سيناريوهات)
make ts     # R14: pollution guard (4 سيناريوهات)
make lt     # R13: late-truth (4 سيناريوهات)
make ce     # cacheex baseline (3 سيناريوهات)
```

### 3.5 استكشاف الأخطاء

**`build first: make -C ../make-x64`:**
```bash
cd ../make-x64
make
cd ../tests
make all
```

**فشل سيناريو معين:**
```bash
# شغّل الهدف وحده
make r20

# افحص السجلات
ls /tmp/r20-*.log
cat /tmp/r20-server.log
```

**منافذ مشغولة:**
```bash
# اقتل العمليات القديمة
pkill -9 -x multics
sleep 2
make all
```

---

## 4. Soak Test

### 4.1 الهدف

إثبات أن الخادم يصمد تحت حمل حيّ لمدة 20 دقيقة بلا انهيار، بنمو RSS محدود.

### 4.2 الأمر

```bash
cd tests

# soak قصير (للتحقق)
SOAK_SECONDS=90 bash soak.sh

# soak رسمي (1200 ثانية = 20 دقيقة)
SOAK_SECONDS=1200 bash soak.sh
```

### 4.3 النتيجة المتوقعة

```
soak: 1200s, RSS ceiling 65536 kB, growth after 2 min 16384 kB
binary: ../bin/multics-r82a-stats-x64
  peer reply pid 1795 port 17102
  ...
  server pid 1814
  HTTP up
  reply peer advertised
  client pid 1878
  baseline RSS at 2 min: 1480 kB
elapsed 1202s, ECMs 382, max RSS 1700 kB, end 1700 kB
SOAK: PASS
```

**المعايير:**
- المدة ≥ 1200 ثانية
- ECMs ≥ 200
- RSS max < 65536 kB
- RSS growth (بعد دقيقتين) < 16384 kB
- لا crash markers في السجل
- العدّادات متطابقة (telnet vs page)

### 4.4 الملفات الناتجة

```
docs/soak-4.2/
├── soak.cfg          # إعداد الخادم
├── server.log        # سجل الخادم
├── client.log        # سجل العميل
├── peer-*.log        # سجلات النظراء (8)
├── rss.txt           # عينات RSS (كل 15 ثانية)
├── telnet-end.txt    # عدادات telnet النهائية
├── page.html         # صفحة / النهائية
└── RESULT.txt        # النتيجة النهائية
```

### 4.5 استكشاف الأخطاء

**`port 17100 is already in use`:**
```bash
pkill -9 -x multics
sleep 2
bash soak.sh
```

**`stats binary missing`:**
```bash
cd ../make-x64
make release-stats
cd ../tests
bash soak.sh
```

**`FAIL: server died during the soak`:**
```bash
cat docs/soak-4.2/server.log | tail -50
# ابحث عن crash markers
```

**`FAIL: RSS grew too much`:**
```bash
cat docs/soak-4.2/rss.txt
# إذا كان النمو خطياً، قد يكون تسريب ذاكرة
```

---

## 5. Memcheck

### 5.1 الهدف

الكشف عن أخطاء الذاكرة (leaks, buffer overflows, data races) باستخدام ASan، TSan، و valgrind.

### 5.2 الأمر

```bash
cd make-x64

# الوحدات فقط
bash memcheck.sh units

# الجلسة الحية فقط
bash memcheck.sh live

# كلاهما
bash memcheck.sh all
```

### 5.3 النتيجة المتوقعة

```
== unit tests ==
compiling ASan+UBSan units
compiling TSan units
compiling valgrind units
asan: 42 passed, 0 failed
tsan: 42 passed, 0 failed
valgrind: 42 passed, 0 failed

== short live session ==
built x64/mem-asan/multics
built x64/mem-tsan/multics
built x64/mem-vg/multics
  ok asan empty-config refused (rc=1)
  ok asan live HTTP / and /profiles, tool rc=0, races=0
  ok tsan empty-config refused (rc=1)
  ok tsan live HTTP / and /profiles, tool rc=0, races=12
  ok valgrind empty-config refused (rc=1)
  ok valgrind live HTTP / and /profiles, tool rc=0, races=0

MEMCHECK: clean
```

**ملاحظة:** TSan قد يُبلغ عن 12 data race — هذا استثناء موثق (D49)، ليس فشلاً.

### 5.4 الأدوات

| الأداة | الوظيفة |
|---|---|
| **ASan** | AddressSanitizer — buffer overflows, use-after-free |
| **UBSan** | UndefinedBehaviorSanitizer — signed overflow, null deref |
| **TSan** | ThreadSanitizer — data races |
| **valgrind** | memory leaks, uninitialized reads |

### 5.5 استكشاف الأخطاء

**`LINK FAIL x64/mem-asan/multics`:**
```bash
# تأكد من أن memcheck.sh يحتوي على كل الملفات
grep 'srcs=' memcheck.sh
# يجب أن يتضمن: peerrep.c, cwconsensus.c, loginthrottle.c
```

**`valgrind: No such file or directory`:**
```bash
# valgrind غير مثبت
sudo apt-get install valgrind
# أو تجاهل valgrind (ASan يغطي معظم الأخطاء)
```

**`FAIL asan live_tool reported an error`:**
```bash
cat docs/memcheck-4.1/asan-live.log | grep -A 10 'ERROR:'
# هذا خطأ حقيقي — افتح issue
```

---

## 6. التشغيل التسلسلي الكامل

### 6.1 الاختبار الكامل (قبل commit)

```bash
# 1. الوحدات
cd make-x64
make test
# متوقع: 1643 ok, 0 fail

# 2. التكامل
cd ../tests
make all
# متوقع: 530 ok, 0 FAIL

# 3. Soak (اختياري — 20 دقيقة)
SOAK_SECONDS=1200 bash soak.sh
# متوقع: PASS

# 4. Memcheck (اختياري — 10 دقائق)
cd ../make-x64
bash memcheck.sh all
# متوقع: clean
```

### 6.2 الاختبار السريع (قبل push)

```bash
# الوحدات + التكامل فقط
cd make-x64
make test
cd ../tests
make all
```

---

## 7. إضافة اختبار جديد

### 7.1 وحدة جديدة

1. أنشئ `make-x64/test_xxx.c`:
   ```c
   #include <stdio.h>
   int main() {
       int pass = 0, fail = 0;
       // اختباراتك
       if (test1) { pass++; printf("  [ ok ] test1\n"); }
       else { fail++; printf("  [FAIL] test1\n"); }
       printf("== %d/%d passed, %d failed ==\n", pass, pass+fail, fail);
       return fail ? 1 : 0;
   }
   ```

2. أضف إلى `make-x64/Makefile`:
   ```makefile
   test: ... x64/test_xxx ...
       ...
       ./x64/test_xxx
   
   x64/test_xxx: test_xxx.c ../src/xxx.h
       $(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_xxx.c
   ```

3. شغّل:
   ```bash
   make test
   ```

### 7.2 سيناريو تكامل جديد

1. أضف هدف إلى `tests/stability.mk`:
   ```makefile
   .PHONY: xxx
   xxx: $(BIN) $(HARNESS)
       @echo "== scenario xxx =="
       # إعداد
       # تشغيل
       # تحقق
       @echo "[ ok ] xxx"
   ```

2. أضف إلى `tests/Makefile`:
   ```makefile
   all: ... xxx ...
   ```

3. شغّل:
   ```bash
   make xxx
   ```

---

## 8. الأسئلة الشائعة

**س: لماذا `make test` يمسح `x64/` في النهاية؟**  
ج: لتوفير المساحة. استخدم `make release-*` للاحتفاظ بالثنائيات.

**س: هل يمكنني تشغيل `make -C tests all` بالتوازي؟**  
ج: لا — الأهداف تتشارك المنافذ والـ binaries. شغّلها تسلسلياً.

**س: لماذا soak يستغرق 20 دقيقة؟**  
ج: D52 حدد 1200 ثانية كمعيار للإغلاق. يمكنك استخدام 90 ثانية للتحقق السريع.

**س: هل valgrind مطلوب؟**  
ج: لا — ASan يغطي معظم الأخطاء. valgrind اختياري.

**س: لماذا TSan يُبلغ عن 12 data race؟**  
ج: استثناء موثق (D49) — races معروفة في قوائم الإحصاءات، لا تؤثر على الصحة.

---

## 9. المراجع

- **[DEPLOYMENT-ar.md](DEPLOYMENT-ar.md)** — دليل النشر
- **[CONFIG-REFERENCE-ar.md](CONFIG-REFERENCE-ar.md)** — مرجع الإعداد
- **[TROUBLESHOOTING-ar.md](TROUBLESHOOTING-ar.md)** — حل المشاكل
- **[DECISIONS.md](DECISIONS.md)** — D52 (soak standard), D49 (TSan exception)
- **`docs/soak-4.2/`** — سجلات soak الرسمية
- **`docs/memcheck-4.1/`** — سجلات memcheck الرسمية
