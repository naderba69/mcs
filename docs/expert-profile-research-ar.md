# ملف البروفايلات المحافظ: المراجع وحدودها

تاريخ المراجعة: 2026-10-29. الناتج `multics-r24-expert-reviewed.cfg` قالب مستقل يضم البروفايلات الـ72؛ مصدر القيم `multics-r24-optimized.cfg` مع تعطيل HTTP في القالب فقط. لم تُغيَّر مهلات أو CAID أو مزوّدات أو قوائم قنوات بالاعتماد على صفحات عامة. إنشاء القالب وتحديثه: `python3 tools/generate-expert-config.py`، والتحقق من تزامنه: `python3 tools/generate-expert-config.py --check`. لا تعدّل القالب مباشرة ثم تعيد توليده؛ عدّل الأصل وراجع الفروق ثم ولّده.

**التشغيل:** الملف لا يحتوي مصادر أو مستخدمين أو مفاتيح CW، وHTTP PORT يساوي 0 ولذلك لا تبدأ واجهة الإدارة. يلزم إعداد المنفذ واسم المستخدم وكلمة مرور طويلة وفريدة على جهازك، وتأمين شبكة الإدارة لأن HTTP غير مشفر، ثم إدخال المصادر والمستخدمين المخوّلين محليًا. افحص التعارض مع المنافذ الموجودة، واحتفظ بنسخة احتياطية قبل أي استبدال. لا تُسجّل الأسرار في Git. هذا قالب يحتاج تهيئة وتحققًا وليس إثبات جاهزية نشر أو ضمان مشاهدة.

## خلاصة المراجع

- [S1](https://git.streamboard.tv/common/oscam/-/wikis/pages/configuration/oscam.server): دليل OSCam يشرح أن قائمة أطوال ECM بالست عشري اختيارية وأنها غير لازمة عادةً؛ لا يعني ذلك أن syntax OSCam يُطبق مباشرةً في MultiCS. في كودنا `cs_accept_ecm` يطبق قائمة `ECM LENGTH` بصورة مستقلة عن `ECM CHECK LENGTH`؛ راجع `src/main.c`.
- [S2](https://git.streamboard.tv/common/oscam/-/work_items/4527): بلاغ تطوير يذكر اختلاف قواعد checksum بحسب بعض مزودات Viaccess؛ هو بلاغ وليس إثباتًا لوضع كل قناة اليوم. لا يجوز التعميم بأن CAID وحده يثبت صلاحية checksum أو الإعفاء منها.
- [S3](https://git.streamboard.tv/common/oscam/-/wikis/pages/configuration/oscam.user): CacheEx واتجاه التدفق والفلاتر وإعفاء checksum خيارات مستقلة في OSCam. لا نستنتج منها تطابق تنفيذ MultiCS أو صلاحية أقرانك. راجع الكود ونتائج المصادر الفعلية.
- [S4](https://en.kingofsat.net/packages.php?id=113&lim=110&standard=All&ordre=freq&filtre=no&aff=zap) Vivacom: صفحة لقنوات تعرض VideoGuard/Irdeto؛ لا تثبت 09BD لكل قناة.
- [S5](https://en.kingofsat.net/find.php?question=Dorcel) Digit Alb، و[Total TV](https://en.kingofsat.net/hdtvpack-totaltv.php): أمثلة خدمات Conax أو أنظمة متزامنة؛ ليست إثباتًا لكل CAID في الملف.
- [S6](https://en.kingofsat.net/packages.php?aff=zap&filtre=no&id=62&lim=30&ordre=nom_ch&standard=All): Digi/Focus تظهر أنظمة متعددة لقنوات مختلفة.
- [S7](https://en.kingofsat.net/find2.php?cl=bul&num_crypt=&num_genre=&num_pays=&num_standard=&ordre=freq&pos=): أمثلة Bulsatcom تعرض Conax/Nagravision؛ هذه الأمثلة لا تثبت صلاحية 4AEE الحالي ولا تستبعده من كل بث آخر.
- [S8](https://www.sat-charts.eu/wykaz_kanalow,Cyfrowy+Polsat,pakiet.html): يعرض لبعض خدمات Polsat كلا 1803 و1861؛ حداثة كل سجل تحتاج مراجعة. هذا لا يحدد منفذًا صالحًا أو إعداد مصدر؛ في ملفنا يقرأ المحلل 1803 وحده.
- [S9](https://en.kingofsat.net/pack-ncplus): يعرض خدمات Platforma Canal+ وأنظمة متعددة، دون أن يثبت صلاحية كل قائمة Provider في بروفايلات NC+.
- [S10](https://en.kingofsat.net/pos-19.2E): صفحة مدارية لـ HD+/Sky DE/Austriasat/ORF تظهر تنوع الأنظمة؛ لا تثبت إعدادات CW أو CAID كل بروفايل.
- [S11](https://en.kingofsat.net/channelhistory.php?ch=753): سجل Movistar مثال على تغيّر خدمة في 2026، وليس سياسة جميع الخدمات.
- [S12](https://en.kingofsat.net/find.php?lim=0&ordre=sid&question=JUNIOR&standard=All): أمثلة MEO/NOS؛ لا تثبت قيم المزوّد أو المهلة.
- [S13](https://en.kingofsat.net/find2.php?cl=ger&num_crypt=&num_genre=&num_pays=&num_standard=&ordre=freq&pos=): مثال SRG؛ لا يتحقق من قائمة SID في الملف.
- [S14](https://en.kingofsat.net/detail-france5-hd.php): أمثلة Canal+/TNTSAT/Fransat تظهر اختلاف أنظمة بث الخدمة حسب الموقع.
- [S15](https://en.kingofsat.net/sat-astra2e): أمثلة Sky Digital؛ لا يثبت CAID أو صيغة CW لكل خدمة.
- [S16](https://en.kingofsat.net/hdtvpack-tivusat): أمثلة TivùSat/Mediaset عبر القمر، ولا تكفي للتحقق من Mediaset DTT الأرضية.
- [S17](https://en.kingofsat.net/pack-canaldigitaal): أمثلة Skylink/Canal Digitaal وأنظمة متزامنة؛ ليست إثباتًا لـ0D96 أو 1817/1818 لكل قناة.
- [S18](https://en.kingofsat.net/channelhistory.php?ch=2697): سجل تاريخي لخدمة في Cosmote/D-Smart؛ لا يمثل بثًا حاليًا لجميع قنواتهما.

المراجع أعلاه **مراجعة سياقية جزئية وليست مراجعة اختصاصي مستقلة لكل باقة**. لم نعثر هنا على أدلة تشغيلية كافية لكل واحدة من الباقات الـ72؛ الصنف «لم يتحقق خارجيًا» واضح أدناه. لا تدعم أي صفحة تحديد أوقاتك المثلى أو ضمان فك الفيديو. اطلع على القيم المكتوبة والواجهة الموروثة لجميع الأقسام في `docs/profile-audit-ar.md`.

## جدول القرار لكل بروفايل

`سياق` يعني وجود مرجع عام عن الاسم لا تصديق CAID/Provider/SID. القيد المذكور مبني على ملف المستودع، لا على قائمة مشغل حديثة. وحدات المهلات ms.

| # | البروفايل | CAID المقروء | تحقق خاص مطلوب | مرجع سياقي |
|---:|---|---|---|---|
| 1 | Vivacom - 09BD | `09BD` | CAID وProvider وصيغة CW والمصدر | [S4] سياق فقط |
| 2 | Focus - 0B02 | `0B02` | CAID وProvider وصيغة CW والمصدر | [S6] سياق فقط |
| 3 | YES+AMOS - 090D | `090D` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 4 | Orange SK - 0609 | `0609` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 5 | DigitalALB - 0B00 | `0B00` | CAID وProvider وصيغة CW والمصدر | [S5] سياق فقط |
| 6 | HD+ 1830 | `1830` | CAID وProvider وصيغة CW والمصدر | [S10] سياق فقط |
| 7 | SAT BG - 0B01 | `0B01` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 8 | TOTAL TV - 091F | `091F` | أطوال ECM مقيدة | [S5] سياق فقط |
| 9 | C+FR - 1883 | `1883` | CAID وProvider وصيغة CW والمصدر | [S14] سياق فقط |
| 10 | DolceTV - 092F | `092F` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 11 | NOS SGPS - 1802 | `1802` | CAID وProvider وصيغة CW والمصدر | [S12] سياق فقط |
| 12 | CSAT NG - 1811 | `1811` | قائمة منع SID | لم يتحقق خارجيًا |
| 13 | COSMOTE - 09BE | `09BE` | CAID وProvider وصيغة CW والمصدر | [S18] سياق فقط |
| 14 | UPC - 1815 | `1815` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 15 | Bulsatcom - 4AEE | `4AEE` | مطابقة 4AEE مع البث | [S7] سياق فقط |
| 16 | Sky DE - 098D | `098D` | قائمة منع SID | [S10] سياق فقط |
| 17 | RCS/DIGI TV - 1880 | `1880` | CAID وProvider وصيغة CW والمصدر | [S6] سياق فقط |
| 18 | OSN - 1708 | `1708` | سماح CAMD35/CS378X موروث | لم يتحقق خارجيًا |
| 19 | DIGI TV - 1802 | `1802` | CAID وProvider وصيغة CW والمصدر | [S6] سياق فقط |
| 20 | AB SAT - 1819 | `1819` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 21 | AUSTRIASAT - 0D95 | `0D95` | أطوال ECM مقيدة | [S10] سياق فقط |
| 22 | POLSAT - 1803/1861 | `1803` | أطوال ECM مقيدة؛ 1861 غير مفعّل | [S8] سياق فقط |
| 23 | FRA - 32830 | `0500` | CAID وProvider وصيغة CW والمصدر | [S14] سياق فقط |
| 24 | BIS TV - 042820 | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 25 | ART - 0604 | `0604` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 26 | MovieStar - 1810 | `1810` | أطوال ECM مقيدة | [S11] سياق فقط |
| 27 | NC+ CYFRA - 0100/68 | `0100` | أطوال ECM مقيدة | [S9] سياق فقط |
| 28 | ORF - 0D98 | `0D98` | CAID وProvider وصيغة CW والمصدر | [S10] سياق فقط |
| 29 | NC+ 4K - 1884 | `1884` | CAID وProvider وصيغة CW والمصدر | [S9] سياق فقط |
| 30 | SRG Swiss + Viaccess - 0500 | `0500` | قائمة سماح SID؛ أطوال ECM مقيدة | [S13] سياق فقط |
| 31 | Skylink - 0D96 | `0D96` | CAID وProvider وصيغة CW والمصدر | [S17] سياق فقط |
| 32 | TNTSAT - 030B00 | `0500` | CAID وProvider وصيغة CW والمصدر | [S14] سياق فقط |
| 33 | MEO PT - 1814 | `1814` | CAID وProvider وصيغة CW والمصدر | [S12] سياق فقط |
| 34 | HD+ - 1843 | `1843` | أطوال ECM مقيدة | [S10] سياق فقط |
| 35 | Eleven - 0100/6A+6C | `0100` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 36 | Movistar+ - 004106 | `0100` | قائمة سماح SID؛ أطوال ECM مقيدة؛ رأس ECM مقيد | [S11] سياق فقط |
| 37 | SkyUK - 0963 | `0963` | CAID وProvider وصيغة CW والمصدر | [S15] سياق فقط |
| 38 | MEO Portugal - 0100/5221 | `0100` | CAID وProvider وصيغة CW والمصدر | [S12] سياق فقط |
| 39 | Mediaset DTT - 1805 | `1805` | أطوال ECM مقيدة؛ مرجع فضائي وليس DTT | [S16] سياق فقط |
| 40 | TivuSat - 183D | `183D` | CAID وProvider وصيغة CW والمصدر | [S16] سياق فقط |
| 41 | 0D97 - 0600 | `0D97` | أطوال ECM مقيدة؛ الاسم يذكر 0600 وCAID الفعلي 0D97 | لم يتحقق خارجيًا |
| 42 | Kabel NDS - 098E | `098E` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 43 | Tring Digital - 0BAA | `0BAA` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 44 | Viasat - 093E | `093E` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 45 | Viasat - 090F | `090F` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 46 | Vision Ukraine - 0500 | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 47 | XTRA TV - 0B0F | `0B0F` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 48 | TVSat - 183E | `183E` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 49 | Telesat - 00006D | `0100` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 50 | MEO Portugal - 5221/5211/5225 | `0100` | أطوال ECM مقيدة | [S12] سياق فقط |
| 51 | SRG/SRF - 0500/508 | `0500` | قائمة سماح SID؛ أطوال ECM مقيدة | [S13] سياق فقط |
| 52 | HD+ 4K - 1860 | `1860` | قائمة منع SID | [S10] سياق فقط |
| 53 | Sky UK - 0960 | `0960` | قائمة سماح SID | [S15] سياق فقط |
| 54 | Virgin Cable UK - 1841 | `1841` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 55 | Love Natural 4K - 0500 | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 56 | D-SMART - 092B | `092B` | CAID وProvider وصيغة CW والمصدر | [S18] سياق فقط |
| 57 | Hayat - 0500 | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 58 | Pink - 0629 | `0629` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 59 | HRT - 0500/16E | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 60 | Ziggo - 0606 | `0606` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 61 | Canal Digital 23.5E - 1818 | `1818` | CAID وProvider وصيغة CW والمصدر | [S17] سياق فقط |
| 62 | Canal Digital 23.5E - 1817 | `1817` | CAID وProvider وصيغة CW والمصدر | [S17] سياق فقط |
| 63 | SCT + Penthouse HD - 0500 | `0500` | أطوال ECM مقيدة | لم يتحقق خارجيًا |
| 64 | Albanicasat - 0606 | `0606` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 65 | YES 4W - 0930 | `0930` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 66 | France Sat - 0500 | `0500` | CAID وProvider وصيغة CW والمصدر | [S14] سياق فقط |
| 67 | Mediaset DTT - 1840 | `1840` | أطوال ECM مقيدة؛ مرجع فضائي وليس DTT | [S16] سياق فقط |
| 68 | NC+ - 1813 | `1813` | CAID وProvider وصيغة CW والمصدر | [S9] سياق فقط |
| 69 | C+NL Nagra - 1817 | `1817` | CAID وProvider وصيغة CW والمصدر | [S17] سياق فقط |
| 70 | Redlight - 0500 | `0500` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 71 | Profile_BISS | `2600` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |
| 72 | Profile_Tandberg | `1010` | CAID وProvider وصيغة CW والمصدر | لم يتحقق خارجيًا |

## التحقق والقيود العملية

- `tests/cfgcheck.py` يفحص قواعد ثابتة محددة وليس صلاحية القنوات.
- `tests/profile-runtime-audit.py --config multics-r24-expert-reviewed.cfg --output <مسار خاص>` يشغّل نسخة مؤقتة ويستبدل فيها منفذ وبيانات HTTP و72 منفذًا، لذلك **لا يختبر إعداد تعطيل HTTP الأصلي**. يقارن حقول الواجهة الظاهرة فقط ويمنع مصادر أو حسابات حقيقية في الاختبار.
- جُرّب القالب أيضًا مع `HTTP PORT: 0` كما هو، ومع منافذ بروفايلات وكاش مؤقتة: أبلغ عن منفذ HTTP غير صالح، ورصدنا 72 مستمعًا مملوكًا للعملية وكلها منافذ البروفايلات المؤقتة، دون مستمع إدارة. `tests/expert-config.py` يحرس التزامن والفروق المقصودة.
- لا توجد بيانات ECM أو CW أو بث فيديو أو مزوّد خدمة حقيقي في هذا الفحص؛ لم تُقَس freeze أو صورة أو أداء إنتاج. لا نعرف القيم المثالية للمهلات ولا يمكن الجزم بأن قوائم SID الموروثة محدثة. الحماية الاستدلالية STRICT/SSF ما زالت معطلة افتراضيًا مؤقتًا، كما في تقرير المحاكاة.
- قبل اعتماد بروفايل، طابق بيانات المصدر والبث المصرح به والـCAID والـProvider وSID وأطوال ECM ومقاييس التأخير وجودة الصورة، ثم اختبر تدريجيًا بخطة تراجع. اختلاف هذه البيانات قد يتطلب تعديل الإعدادات أو الكود، لا مجرد تبديل رقم.
