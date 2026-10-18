# تحليل ملف البروفايلات الإنتاجي — R53 (سطر بسطر)

**الملف:** `multics-r24-optimized.cfg` — 72 بروفايلاً، 2042 سطراً قبل التعديل.
**المرجع:** النص الملصوق في المحادثة فقط (مصدر مقارنة، غير محفوظ في المشروع، بلا أي بيانات اعتماد).

## 1. ما تأكّد حياً (تشغيل فعلي، لا قراءة فقط)

| الادعاء | النتيجة الحية | الحكم |
|---|---|---|
| `FILTER: NO` على المستوى العام يطفئ الفلتر | صفحة `/cache` تُظهر `Filter = ON` | **خاطئ** — المفتاح مُتجاهَل |
| `CACHE FILTER: OFF` يطفئ الفلتر | `Filter = OFF` | **صحيح** — هذا هو المفتاح الفعلي |
| `FILTER` / `FILTER TIME` داخل البروفايلات | لا معالج لهما في `src/config.c` إلا تحت `CACHE` | **مُتجاهَلان** (14 بروفايلاً في المرجع) |
| `ACCEPT NULL ONID` قبل أي بروفايل | رسالة `Skip ACCEPT, undefined profile` | **مُتجاهَل** |
| `DCW VALIDECMTIME` في ملفنا | غير موجود في أي بروفايل (= 0 = مُعطّل) | الوضع المستقر (opt-in) |
| `SERVER VALIDECMTIME` | مفتاح مختلف: زمن رد الخادم الأقصى، وليس R44 | مُستخدَم بشكل صحيح |

**تصحيح لـR40:** ادعاء أن `FILTER: NO` و`FILTER TIME: 10` يعملان كان خاطئاً. ملفنا كان يعمل فعلياً بالافتراض (فلتر `ON`، نافذة `0`) بينما يُظهر تعليقه نافذة 10 ثوانٍ. هذا التعليق كان مضلِّلاً.

**خطر في المرجع:** المرجع يضع `DCW VALIDECMTIME 1200` افتراضياً، فيُرفض أي مفتاح يصل بعد 1.2 ثانية في البروفايلات التي لا تعرّفه. هذا يكسر بروفايلات مهلتها 2000–5000 ms. ملفنا لا يحمل هذا الخطر.

## 2. التعديلات المنفّذة (تحافظ على السلوك الحي)

1. **`multics-r24-optimized.cfg`**: استُبدل `FILTER: NO` و`FILTER TIME: 10` (الميتان) بـ`CACHE FILTER: ON` و`CACHE FILTER TIME: 0`. هذه القيم هي الحالة الجارية فعلاً، فلا يتغيّر أي سلوك. وحُذف `ACCEPT NULL ONID: YES` العام الميت (قيمته = الافتراض).
2. **`tests/cfgcheck.py`**: كان يُلزم بمفتاح `FILTER` العام الميت ويرفض `CACHE FILTER`. أصبح يعدّ `FILTER`/`FILTER TIME`/`ACCEPT NULL ONID` العامة مفاتيح ميتة، ويطلب `CACHE FILTER`.
3. **`MANIFEST-md5`**: حُدّثت تجزئتا الملفين المتغيّرين؛ `md5sum -c` = 576 OK.

## 3. التحقق

- `python3 tests/cfgcheck.py` ⇒ **6/6 ok**.
- `make -C tests prof` ⇒ **5/5 ok** (72 منفذاً يستمع، الكاش/الإثبات مُفعّل، لا تعطل).
- تشغيل حي بـ`-v`: صفر رسائل `Skip`، و74 سطراً يخص البروفايلات، وتحذيران فقط لمنفذي Telnet وFreeCCcam المعطّلين عمداً (منفذ 0).

## 4. قرارات تنتظر المستخدم (لم تُنفَّذ — تغيّر السلوك الحي)

1. **نافذة الفلتر**: `CACHE FILTER TIME` حالياً 0. قيمة 10 (ما كان يُقصد) تغيّر حياة الكاش. يحتاج قياساً.
2. **`CACHE TIMEOUT: 250`** موحّد في الـ72 بروفايلاً، بينما المرجع يتدرّج 150–1200. لا قياس لزمن الكاش بعد، فلا يُغيَّر.
3. **`SERVER TIMEOUT: 800`** في 62 بروفايلاً. المرجع يضع 2000–4500 لأغلبها. القيمة الحالية نتيجة قياس R45 للبطيء فقط، وبقية الباقات ليست مقاسة بالمهلة الأطول.
4. **`ECM CHECK LENGTH: NO`** في 21 بروفايلاً (الافتراض في الكود = YES). سبب التعطيل غير موثّق، ولا يُغيَّر دون دليل.
5. **`DCW MAXFAILED: 3`** (المرجع 0). الملف يعلنها «أرضية أمان موثّقة لا تُخفَض» (سطر 42)، فلا تُغيَّر. القيمة 0 في المرجع تعني أن الحظر مُطفأ.
6. **القناة `1884:000000:1136`**: لم تُغيَّر (قرار المستخدم).
7. **`CA MODERN`** في المرجع: لم أتحقق من وجوده في الـ72 بروفايلاً في المرجع. أعلام iCam في الجدول أدناه تحمل علامة `?` لهذا السبب.

## 5. الجدول الكامل (كل بروفايل)

الأعلام: `MINT` = MINTIME>0؛ `MAXF0` = MAXFAILED صفر؛ `DVALID<STO` = VALIDECMTIME أقصر من مهلة الخادم؛ `GUARD-OFF` = فحص الدورة معطّل؛ `HALF-nonVG` = نصف-المفتاح خارج VideoGuard؛ `SKIP-NO` = لا فحص قبل الإرسال؛ `CYC-NO` = بوابة الكاش معطّلة؛ `ECMLEN-list` = قائمة أطوال ECM (بيانات المشغّل)؛ `NOPORT` = بلا منفذ؛ `FILTER-dead` = مفتاح ميت.

| # | البروفايل | CAID | أعلام ملفنا | أعلام المرجع |
|---|---|---|---|---|
| 1 | Vivacom - 09BD | 09BD | — | MINT150, MAXF0, DVALID<STO(800<2000) |
| 2 | Focus - 0B02 | 0B02 | — | MINT100, MAXF0, DVALID<STO(1200<1500) |
| 3 | YES+AMOS - 090D | 090D | — | MAXF0 |
| 4 | Orange SK - 0609 | 0609 | — | MINT150, MAXF0, DVALID<STO(1200<2000) |
| 5 | DigitalALB - 0B00 | 0B00 | — | MINT100, MAXF0, CYC-NO, FILTER-dead |
| 6 | HD+ 1830 | 1830 | — | MINT250, MAXF0, DVALID<STO(1200<2000) |
| 7 | SAT BG - 0B01 | 0B01 | — | MINT150, MAXF0, DVALID<STO(800<2000), FILTER-dead |
| 8 | TOTAL TV - 091F | 091F | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<3500), ECMLEN-list |
| 9 | C+FR - 1883 | 1883 | — | MINT100, MAXF0, DVALID<STO(1200<4500) |
| 10 | DolceTV - 092F | 092F | — | MAXF0, SKIP-NO, CYC-NO, FILTER-dead |
| 11 | NOS SGPS - 1802 | 1802 | — | MINT100, MAXF0, CYC-NO, FILTER-dead |
| 12 | CSAT NG - 1811 | 1811 | — | MINT250, MAXF0, DVALID<STO(1200<4500) |
| 13 | COSMOTE - 09BE | 09BE | — | MINT100, MAXF0 |
| 14 | UPC - 1815 | 1815 | — | MINT100, MAXF0, CYC-NO, FILTER-dead |
| 15 | Bulsatcom - 4AEE | 4AEE | — | MINT100, MAXF0, DVALID<STO(1200<2000) |
| 16 | Sky DE - 098D | 098D | — | MAXF0, iCAM-CAMMODERN? |
| 17 | RCS/DIGI TV - 1880 | 1880 | — | MINT100, MAXF0, HALF-nonVG, CYC-NO, FILTER-dead |
| 18 | OSN - 1708 | 1708 | — | MAXF0, HALF-nonVG |
| 19 | DIGI TV - 1802 | 1802 | — | MAXF0, SKIP-NO, CYC-NO, FILTER-dead |
| 20 | AB SAT - 1819 | 1819 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 21 | AUSTRIASAT - 0D95 | 0D95 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<2500), ECMLEN-list |
| 22 | POLSAT - 1803/1861 | 1803, 1861 | ECMLEN-list | MINT100, MAXF0, ECMLEN-list, CYC-NO, FILTER-dead |
| 23 | FRA - 32830 | 0500 | — | MINT150, MAXF0, DVALID<STO(1200<2500) |
| 24 | BIS TV - 042820 | 0500 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 25 | ART - 0604 | 0604 | — | MINT200, MAXF0, DVALID<STO(1200<3000) |
| 26 | MovieStar - 1810 | 1810 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<3500), ECMLEN-list |
| 27 | NC+ CYFRA - 0100/68 | 0100 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<3500), ECMLEN-list |
| 28 | ORF - 0D98 | 0D98 | — | NOPORT, MINT100, MAXF0, DVALID<STO(1200<2500) |
| 29 | NC+ 4K - 1884 | 1884 | — | MAXF0, HALF-nonVG, CYC-NO, FILTER-dead |
| 30 | SRG Swiss + Viaccess - 0500 | 0500 | ECMLEN-list | MINT100, DVALID<STO(1200<2500), ECMLEN-list |
| 31 | Skylink - 0D96 | 0D96 | — | MINT100, MAXF0, DVALID<STO(1200<3000) |
| 32 | TNTSAT - 030B00 | 0500 | — | MAXF0, SKIP-NO, CYC-NO, FILTER-dead |
| 33 | MEO PT - 1814 | 1814 | — | MINT200, MAXF0, DVALID<STO(800<1500), FILTER-dead |
| 34 | HD+ - 1843 | 1843 | ECMLEN-list | MINT250, MAXF0, DVALID<STO(1200<2500), ECMLEN-list |
| 35 | Eleven - 0100/6A+6C | 0100 | — | MAXF0, SKIP-NO, CYC-NO, FILTER-dead |
| 36 | Movistar+ - 004106 | 0100 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<3000), ECMLEN-list |
| 37 | SkyUK - 0963 | 0963 | — | MAXF0, iCAM-CAMMODERN? |
| 38 | MEO Portugal - 0100/5221 | 0100 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 39 | Mediaset DTT - 1805 | 1805 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<4000), ECMLEN-list |
| 40 | TivuSat - 183D | 183D | — | MINT100, MAXF0, DVALID<STO(1200<2000) |
| 41 | 0D97 - 0600 | 0D97 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<3500), ECMLEN-list |
| 42 | Kabel NDS - 098E | 098E | — | MAXF0, iCAM-CAMMODERN? |
| 43 | Tring Digital - 0BAA | 0BAA | — | MINT100, MAXF0, CYC-NO, FILTER-dead |
| 44 | Viasat - 093E | 093E | — | MAXF0 |
| 45 | Viasat - 090F | 090F | — | MAXF0 |
| 46 | Vision Ukraine - 0500 | 0500 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 47 | XTRA TV - 0B0F | 0B0F | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 48 | TVSat - 183E | 183E | — | MINT100, MAXF0, DVALID<STO(1200<2000) |
| 49 | Telesat - 00006D | 0100 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 50 | MEO Portugal - 5221/5211/5225 | 0100 | ECMLEN-list | MINT250, MAXF0, DVALID<STO(1200<2500), ECMLEN-list |
| 51 | SRG/SRF - 0500/508 | 0500 | ECMLEN-list | MINT250, MAXF0, DVALID<STO(1200<2500), ECMLEN-list |
| 52 | HD+ 4K - 1860 | 1860 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 53 | Sky UK - 0960 | 0960 | — | MAXF0 |
| 54 | Virgin Cable UK - 1841 | 1841 | — | MAXF0, HALF-nonVG |
| 55 | Love Natural 4K - 0500 | 0500 | — | MINT300, MAXF0, DVALID<STO(1200<4500) |
| 56 | D-SMART - 092B | 092B | — | MAXF0 |
| 57 | Hayat - 0500 | 0500 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 58 | Pink - 0629 | 0629 | — | MINT100, MAXF0, DVALID<STO(1200<2000) |
| 59 | HRT - 0500/16E | 0500 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 60 | Ziggo - 0606 | 0606 | — | MINT100, MAXF0, DVALID<STO(1200<3000) |
| 61 | Canal Digital 23.5E - 1818 | 1818 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 62 | Canal Digital 23.5E - 1817 | 1817 | — | MINT250, MAXF0, DVALID<STO(1200<2500) |
| 63 | SCT + Penthouse HD - 0500 | 0500 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<5000), ECMLEN-list |
| 64 | Albanicasat - 0606 | 0606 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 65 | YES 4W - 0930 | 0930 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 66 | France Sat - 0500 | 0500 | — | MINT100, MAXF0, DVALID<STO(1200<2500) |
| 67 | Mediaset DTT - 1840 | 1840 | ECMLEN-list | MINT100, MAXF0, DVALID<STO(1200<4000), ECMLEN-list |
| 68 | NC+ - 1813 | 1813 | — | MINT100, MAXF0, CYC-NO, FILTER-dead |
| 69 | C+NL Nagra - 1817 | 1817 | — | MINT100, MAXF0, DVALID<STO(1200<3500) |
| 70 | Redlight - 0500 | 0500 | — | MINT100, MAXF0, DVALID<STO(1200<5000) |
| 71 | Profile_BISS | 2600 | — | MINT100, MAXF0 |
| 72 | Profile_Tandberg | 1010 | — | MINT100, MAXF0 |

## 6. حدود هذا التقرير

- أعلام المرجع مُستخرجة من النص الملصوق، ولم تُقَس حياً بالبروفايل.
- التحقق الحي لكل بروفايل هو التحليل الساكن وتشغيل الملف (`prof`). لا يوجد قياس لزمن الاستجابة لكل قناة هنا.
- قيم `ECM LENGTH` وأي قيمة مهلة هي بيانات مشغّل، ولا تُغيَّر دون قياس.
