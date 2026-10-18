# تدقيق محركات الحماية لكل باقة — R62 (2026-10-18)

## ماذا فحصنا
ثلاث طبقات، ولكل طبقة أداة أو اختبار قابل للتكرار:

1. **الإعدادات لكل بروفايل** — `tests/protection_audit.py`: يحسب القيمة **الفعلية** لكل مفتاح حماية في كل بروفايل من الـ72، بترتيب المفسّر نفسه:
   1. سطر البروفايل نفسه (يفوز)،
   2. سطر `DEFAULT` في القسم العام (يُنسخ عند بداية البروفايل)،
   3. الافتراض المدمج في `src/config.c` (`init_cardserver`).
   ثم يقارن كل قيمة بقاعدة عائلتها (من `src/casystem.h`).
2. **الشاهد الحي** — نفس الأداة بالخيار `--live`: تسأل خادماً يعمل عن `/profiles` ثم `/profile?id=N` لكل بروفايل، وتقارن ما تعرضه الصفحة بما يقوله الملف. الصفحة لا تعرض كل المفاتيح، فالمفاتيح غير المعروضة تُعدّ «غير مرئية حياً» ولا تُعدّ مُتحقَّقاً منها.
3. **المحركات نفسها** — الاختبارات الحية والوحدوية الموجودة في المستودع، تُشغَّل على الثنائي الحالي `bin/multics.x64`.

## النتيجة المختصرة
| الفحص | النتيجة |
|---|---|
| اختبارات المحركات الحية (22 هدفاً) | **22/22 نجحت** بلا أي إخفاق |
| اختبارات الوحدات `make test` | **25/25** |
| اختبارات بوابات CACHEEX `test_cacheex_gates` | **24/24** |
| قيم الإعدادات الفعلية لكل بروفايل مقابل قاعدة العائلة | **51 بروفايلاً صحيحاً، 21 بروفايلاً معطّل فيها فحص طول ECM** |
| المقارنة الحية (`/profile`) | **864 قيمة مُقارَنة، 0 اختلاف** (72 بروفايلاً × 12 مفتاحاً معروضاً) |
| اختبار سلبي للمقارنة الحية | المرشح `multics-r56-fresh.cfg` أعطى **69 اختلافاً** في `CACHE STATIC`، أي أن المقارنة تكشف الفرق فعلاً |

**الخلاصة الصريحة:** محركات الحماية الموصولة بمسار القرار تعمل في الاختبارات. لكن التدقيق كشف ثلاث مشكلات حقيقية:
- **محركات تبدو حماية ولا تؤثر على القرار** (قسم 3 أدناه)، وأخطرها رسالة «triggering quick switch» التي لا يتبعها أي تبديل.
- **21 بروفايلاً يعطّل فحص طول ECM** دون توثيق سبب (قسم 2).
- **8 مفاتيح حماية لا تظهر في صفحة البروفايل**، فلا يمكن التحقق منها حياً (قسم 4).

## 1. المحركات الموصولة وما يثبت أنها تعمل
| المحرك | المفتاح/المفاتيح | الاختبار | النتيجة |
|---|---|---|---|
| فحص البنية والـchecksum | `DCW CHECK`، `ENABLE SKIPCWC` (يفرض الـchecksum على CSA) | `sc` 9/9، `fz` 2/2، `entropy` 12/12، `plaus` 21/21 | يعمل |
| بوابة الدورة | `DCW CYCLE_CHECK: YES` | `cyc` 14/14 | يعمل |
| الإجماع والدرع الموحّد | `CACHE PROTECTION: STANDARD` | `cm` 16/16، `cn` 4/4، `at` 3/3 | يعمل |
| بوابة الشكل المزيّف | `DCWFILTER PATTERN` (مُفعَّل عاماً بـSTANDARD) | `fk` 4/4، `fz` 2/2 | يعمل |
| الذاكرة السلبية وثقة النظراء | `PEER REPUTATION: ON` | `tl` 15/15، `pt` 15/15 | يعمل |
| حارس الكاش وإعادة الاستخدام | إثبات إعادة الاستخدام وحارس الكاش | `cwreuse` 8/8، `cachecw` 8/8، `cg` 12/12، `purgeage` 13/13، `agreement` 13/13 | يعمل |
| الأنظمة الحديثة الموحّدة | `CA MODERN: ON` | `md` 3/3 | يعمل |
| التوقيت والحلقة | `DCW MINTIME`، `DCW VALIDECMTIME`، `ECMLOOPING` | `timing` 16/16، `r44` (نجح في R60) | يعمل |
| بوابات CACHEEX | `BLOCK_FAKE_CW`، `CWCHECK`، `LOCAL_ONLY` | `test_cacheex_gates` 24/24 (وحدات) | يعمل في الوحدات، لم يُختبر حياً هنا |
| الهيكل والبيانات | `dstruct`، `rt`، `cf`، `dl` | 8/8، 3/3، 2/2، 7/7 | يعمل |

## 2. مشكلة مؤكدة: فحص طول ECM مُعطّل في 21 بروفايلاً
المفتاح `ECM CHECK LENGTH` افتراضه `YES` في الكود (`init_cardserver`: `checkecmlength = 1`). 21 بروفايلاً تضبطه على `NO` **دون أي تعليق يذكر السبب**. النتيجة أن ECM بطول غير متوقع يمرّ إلى الخادم في هذه القنوات، ومنها **NC+ 4K ‏(1884)** و**Sky DE ‏(098D)**.

- **5 بروفايلات تكتب قائمة `ECM LENGTH` لا تُفرض** لأن الفحص معطّل: `TOTAL TV`، `POLSAT`، `MovieStar`، `Mediaset DTT 1805`، `Mediaset DTT 1840`. القائمة موجودة في الملف لكنها بلا أثر.
- **لا أغيّر هذا الآن.** التوثيق السابق يقول إن التفعيل بلا قياس قد يرفض ECM صحيحاً وينقل الانقطاع من قناة إلى أخرى (`docs/AUDIT-R52-ar.md`، و`docs/FRESH-CONFIG-R57-ar.md` سطر 23). التفعيل يحتاج قياس أطوال ECM الحقيقية لكل قناة أولاً.

## 3. محركات تبدو حماية ولا تؤثر على القرار (الكود)
هذه نتيجة من قراءة الكود وعدّ الاستدعاءات (بحث نصّي عن كل استدعاء في `src/`). الاستدعاء الصفري يعني أن الدالة لا تُنفَّذ في الثنائي الإنتاجي:

| المحرك | ما يفعله الكود فعلاً | الحالة |
|---|---|---|
| `dcw_on_failure` → «triggering quick switch» و«Emergency mode activated» (`src/dcw.c`) | تعدّ الفشل وتطبع الرسالتين فقط. **لا تبدّل خادماً ولا قناةً.** | **تسجيل مضلِّل** |
| `dcw_needs_emergency` (`src/dcw.c`) | الدالة التي قد تتخذ قرار الطوارئ — **لا يستدعيها أحد**. | **ميتة** |
| `server_health_*` (`src/server-health.h`) | تحسب `health_score` لكل خادم، ولا يقرأها أي قرار توجيه. | **تسجيل فقط** |
| `blackscreen-detector.h` / `performance-suite.h` | `performance-suite.h` غير مُضمَّن في أي ملف، و`bs_record_cw_failure` غير مستدعاة (التحذير أثناء `make test`). | **ميت** |
| `auto_recovery_*`، `predictive_warming_should_warm`، `prefetch_should_prefetch`، `parallel_ecm_start`، `cache_priority_get`، `cw_prediction_get_last`، `server_health_get_score`، `server_health_is_lazy`، `server_health_get_timeout` | صفر استدعاءات | **ميتة** |

**الأثر على هدفنا (الشاشة السوداء):** لا يوجد في الإنتاج أي منطق يبدّل القناة أو الخادم عند تكرار الفشل. الرسالة «triggering quick switch» تُوهم بوجود إجراء لم يحدث. وهذا لا يطابق ما يعد به السجل. يجب أن يُصلَح قبل أي حديث عن حماية الشاشة السوداء بالتبديل التلقائي. لم أُعدّل هذا الكود في هذه الجولة لأن الإصلاح الكامل يحتاج قراراً تصميمياً: ربط القرار بالتوجيه فعلاً مع اختبار حي يثبت أثره.

## 4. مفاتيح حماية لا تظهر في صفحة البروفايل
الصفحة تعرض 12 مفتاحاً من المفاتيح المهمة، والمقارنة الحية غطّتها كلها (864 قيمة). **8 مفاتيح لا تظهر فيها**، لذلك تُعدّ مُدقَّقة من الملف فقط، لا حياً:
`CA MODERN`، `DCW CYCLE_CHECK`، `ENABLE SKIPCWC`، `DCWFILTER PATTERN`، `DCW MINTIME`، `CACHEEX BLOCK_FAKE_CW`، `CACHEEX CWCHECK`، `CACHEEX LOCAL_ONLY`.

الإصلاح المقترح (لم يُنفَّذ): إضافة هذه الصفوف إلى صفحة `/profile` في `src/httpserver.c`، فتصبح كل محركات الحماية قابلة للتحقق حياً من الصفحة نفسها في كل تدقيق لاحق.

## 5. الجدول الكامل لكل بروفايل (القيم الفعلية بعد الوراثة)
القيم: `YES`/`NO` للمفاتيح المنطقية، وأرقام لغيرها. `ECM LEN` = `ECM CHECK LENGTH`، `LOOPING` = `ENABLE ECMLOOPING`، `CW-CHECK` = `CACHEEX CWCHECK`، `BLOCK_FAKE` = `CACHEEX BLOCK_FAKE_CW`.

| # | profile | CAID | family | CA MODERN | HALFNULLED | SWAP | CYCLE | PATTERN | SKIPCWC | MINTIME | VALIDECMTIME | MAXFAILED | ECM CHECK | ECM LEN | LOOPING | CACHE STATIC | CW-CHECK | BLOCK_FAKE | LOCAL_ONLY |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Vivacom - 09BD | 09BD | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 2 | Focus - 0B02 | 0B02 | Conax | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 3 | YES+AMOS - 090D | 090D | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 4 | Orange SK - 0609 | 0609 | Irdeto | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 5 | DigitalALB - 0B00 | 0B00 | Conax | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 6 | HD+ 1830 | 1830 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 7 | SAT BG - 0B01 | 0B01 | Conax | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 8 | TOTAL TV - 091F | 091F | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | YES | NO | YES | NO | 2 | YES | NO |
| 9 | C+FR - 1883 | 1883 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 10 | DolceTV - 092F | 092F | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 11 | NOS SGPS - 1802 | 1802 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 12 | CSAT NG - 1811 | 1811 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | NO | YES | NO | 2 | YES | NO |
| 13 | COSMOTE - 09BE | 09BE | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 14 | UPC - 1815 | 1815 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 15 | Bulsatcom - 4AEE | 4AEE | DRE-Crypt | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 16 | Sky DE - 098D | 098D | VideoGuard iCam | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 17 | RCS/DIGI TV - 1880 | 1880 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 18 | OSN - 1708 | 1708 | BetaCrypt | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 19 | DIGI TV - 1802 | 1802 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 20 | AB SAT - 1819 | 1819 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 21 | AUSTRIASAT - 0D95 | 0D95 | CryptoWorks | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 22 | POLSAT - 1803/1861 | 1803 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 23 | FRA - 32830 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 24 | BIS TV - 042820 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 25 | ART - 0604 | 0604 | Irdeto | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 26 | MovieStar - 1810 | 1810 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 27 | NC+ CYFRA - 0100/68 | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 28 | ORF - 0D98 | 0D98 | CryptoWorks | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 29 | NC+ 4K - 1884 | 1884 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 30 | SRG Swiss + Viaccess - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 31 | Skylink - 0D96 | 0D96 | CryptoWorks | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 32 | TNTSAT - 030B00 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 33 | MEO PT - 1814 | 1814 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 34 | HD+ - 1843 | 1843 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 35 | Eleven - 0100/6A+6C | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 36 | Movistar+ - 004106 | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 37 | SkyUK - 0963 | 0963 | VideoGuard iCam | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 38 | MEO Portugal - 0100/5221 | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 39 | Mediaset DTT - 1805 | 1805 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 40 | TivuSat - 183D | 183D | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 41 | 0D97 - 0600 | 0D97 | CryptoWorks | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 42 | Kabel NDS - 098E | 098E | VideoGuard iCam | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 43 | Tring Digital - 0BAA | 0BAA | Conax | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 44 | Viasat - 093E | 093E | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 45 | Viasat - 090F | 090F | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 46 | Vision Ukraine - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 47 | XTRA TV - 0B0F | 0B0F | Conax | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 48 | TVSat - 183E | 183E | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 49 | Telesat - 00006D | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 50 | MEO Portugal - 5221/5211/5225 | 0100 | Seca | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 51 | SRG/SRF - 0500/508 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 52 | HD+ 4K - 1860 | 1860 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 53 | Sky UK - 0960 | 0960 | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 54 | Virgin Cable UK - 1841 | 1841 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 55 | Love Natural 4K - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 56 | D-SMART - 092B | 092B | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 57 | Hayat - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 58 | Pink - 0629 | 0629 | Irdeto | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 59 | HRT - 0500/16E | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 60 | Ziggo - 0606 | 0606 | Irdeto | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 61 | Canal Digital 23.5E - 1818 | 1818 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 62 | Canal Digital 23.5E - 1817 | 1817 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | YES | YES | YES | NO | 2 | YES | NO |
| 63 | SCT + Penthouse HD - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 64 | Albanicasat - 0606 | 0606 | Irdeto | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 65 | YES 4W - 0930 | 0930 | VideoGuard | YES | YES | YES | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 66 | France Sat - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 67 | Mediaset DTT - 1840 | 1840 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | NO | YES | NO | 2 | YES | NO |
| 68 | NC+ - 1813 | 1813 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 69 | C+NL Nagra - 1817 | 1817 | Nagravision | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 70 | Redlight - 0500 | 0500 | Viaccess | YES | NO | NO | YES | YES | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 71 | Profile_BISS | 2600 | BISS1 (fixed key) | YES | NO | NO | YES | NO | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |
| 72 | Profile_Tandberg | 1010 | Tandberg (fixed key) | YES | NO | NO | YES | NO | YES | 0 | 0 | 3 | NO | YES | YES | NO | 2 | YES | NO |

**قراءة الجدول:**
- `CA MODERN` = `YES` في الـ72 كلها، و`CYCLE` = `YES` في الـ72، و`SKIPCWC` = `YES` في الـ72، و`MAXFAILED` = 3 في الـ72، و`MINTIME` = 0 في الـ72.
- `PATTERN` = `YES` (بالوراثة من `CACHE PROTECTION: STANDARD`) في 70 بروفايلاً، و`NO` **مقصود** في `Profile_BISS` و`Profile_Tandberg` لأن مفتاحيهما الثابتين متطابقان النصفين ونص الترويسة يشرح ذلك.
- `HALFNULLED`/`SWAP` = `YES` في VideoGuard القياسي (09xx غير iCam)، و`NO` في باقي العائلات، وهذا يطابق قاعدة العائلة.
- `CACHEEX LOCAL_ONLY` = `NO` في الكل، وهو قرار موثّق في الترويسة (تفعيله يمنع بذور التسخين الاستباقية).

## 6. ملاحظات منهجية
- **POLSAT** يحمل CAIDين (`1803, 1861`). الأداة تحسب العائلة من الأول.
- الأداة تفشل بخروج `1` عند وجود انتهاكات، وهذا متعمّد: انتهاكات `ECM CHECK LENGTH` الـ21 موجودة فعلاً في الإنتاج.
- الأمر: `python3 tests/protection_audit.py multics-r24-optimized.cfg` (فحص الملف)، و`... --live http://127.0.0.1:PORT user:pass` (مع خادم يعمل على الملف نفسه).

## 7. ما لم يُثبَت بعد
- لم أختبر أي قناة مع خوادم البطاقات الحقيقية (لا توجد شبكة إليها من هذه البيئة). كل الإثبات عبر أدوات محلية.
- صحة قاعدة العائلة نفسها (مثلاً أن قناة معينة تحتاج `HALFNULLED: NO`) مأخوذة من `src/casystem.h` وترويسات الملف، ولم تُقَس على البث.
- البحث عن الاستدعاءات نصّي؛ دالة تُستدعى عبر مؤشر دالة أو ماكرو قد لا تظهر. الحالات المذكورة تحققت منها بالقراءة اليدوية للاستخدامات.

## 8. التوصيات (مرتبة حسب الأثر على الشاشة السوداء)
1. **تصحيح `dcw_on_failure`** — **نُفِّذ في R63**: رسالتا «quick switch» و«Emergency» استُبدلتا بسطر يقول الحقيقة (عدّاد فقط، دون إجراء). لم يتغير السلوك. يبقى ربط قرار الطوارئ بالتوجيه فعلاً مسألة مستقلة تحتاج اختباراً حياً، وقد سبق أن رُفضت تصاميم معاقبة الخادم عبر هذه الدالة (`docs/DECISIONS.md`).
2. **قياس أطوال ECM** للقنوات الـ21 (تبدأ بـ1884 و098D)، ثم تفعيل `ECM CHECK LENGTH` بقائمة مُقاسة، أو توثيق سبب التعطيل داخل الملف.
3. **إضافة المفاتيح الثمانية إلى صفحة البروفايل** حتى يصبح التدقيق الحي كاملاً.
4. **حذف الكود الميت** (`performance-suite.h`، والدوال غير المستدعاة) أو توثيقه كـ«غير مُفعَّل»، حتى لا يُحسب كحماية في أي تقرير.
