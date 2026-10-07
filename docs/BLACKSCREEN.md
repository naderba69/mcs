# MultiCS r82a — لماذا تظهر الشاشة السوداء في بعض الباقات

**كل سطر مذكور هنا مُتحقَّق منه من المصدر الفعلي** في `mcs/src/` (نسخة r82a
المسحوبة من `http://infosat.org/multics/r82src.php`) ومن الاختبار
`mcs/make-x64/test_dcw.c` الذي ينفّذ `dcw.c` الحقيقي (8/8 نجاح).

المبدأ: أي CW يُرفض داخل الخادم لا يصل أبداً إلى الرسيفر ← الرسيفر بلا مفتاح
فكّ التشفير ← **شاشة سوداء**. لذلك كل نقطة رفض خاطئة = شاشة سوداء.

---

## السبب 1 — `isbadDCW()` يرفض مفاتيح صحيحة — **الخيار العام منذ 1.10b، والتجاوز لكل profile أُغلق في TASK 3.15 (2026-09-26)**

`dcw.c:32`:

```c
int isbadDCW(uint8_t *data)
{
	if ( data[0]!=0 && data[0]==data[1] && data[0]==data[2] ) return 1;
	if ( data[4]!=0 && data[4]==data[5] && data[4]==data[6] ) return 1;
	if ( data[8]!=0 && data[8]==data[9] && data[8]==data[10] ) return 1;
	if ( data[12]!=0 && data[12]==data[13] && data[12]==data[14] ) return 1;
	return 0;
}
```

أي CW يحتوي ثلاث بايتات متساوية غير صفرية داخل مجموعة من 4 بايتات يُرفض.
احتمال ذلك عشوائياً ≈ 1/65536 لكل مجموعة، أي ≈ 1/16384 للـ CW كاملة — لكن
بعض المزوّدين يولّدون مفاتيح بهذا النمط بانتظام، فتسودّ **باقة كاملة** بينما
تعمل بقية الباقات. هذا يطابق تماماً عرض «بعض الباقات فقط».

**مُثبت بالاختبار** (`make test`):

```
info : CW with 11 11 11 group -> isbadDCW=1 acceptDCW=0
[ ok ] CW containing three equal bytes is REJECTED (documents current bug) = 0
```

**لا يوجد أي خيار إعداد لإيقافه.** تحقّقت: `DCW CHECK`
(`config.c:3577`, يُستخدم فقط في `setdcw.c:230` و`setdcw.c:567`) يتحكّم
بـ *checkfreeze* فقط، ولا علاقة له بـ `checksumDCW`/`isbadDCW`.

منذ 1.10b البوابة العامّة `DCWFILTER REPEAT` موجودة. منذ 3.15 يمكن
وضع السطر نفسه داخل بروفايل واحد: `OFF` يوقف الفحص لتلك الباقة، وبلا
سطر تبقى البوابة العامّة. المفتاح الفارغ والمفتاح المكتوب في `BAD-DCW`
لا يُتجاوزان.

---

## السبب 2 — فحص الـ checksum كان إلزامياً — **الخيار العام منذ 1.10b، والتجاوز لكل profile أُغلق في TASK 3.15 (2026-09-26)**

`dcw.c:41`:

```c
int acceptDCW(uint8_t *data)
{
	if (!checksumDCW(data)) return 0;   // <- إلزامي، قبل أي خيار profile
	if ( isnullDCW(data) )  return 0;
	if ( isbadDCW(data) )   return 0;
	...
	return 1;
}
```

`checksumDCW` (`dcw.c:35`) يفرض نمط DVB-CSA القياسي
`b3 = (b0+b1+b2) & 0xFF` على المجموعات الأربع. الباقات التي تستعمل checksum
معدّلاً أو غير قياسي تُرفض مفاتيحها الصحيحة دائماً → شاشة سوداء دائمة لتلك
الباقات.

`acceptDCW` يُستدعى من **16 موضعاً** (تحقّق: `grep -rn acceptDCW *.c`):

| الملف | العدد | | الملف | العدد |
|---|---|---|---|---|
| setdcw.c | 2 | | srv-cccam.c | 1 |
| cli-cccam.c | 2 | | srv-camd35.c | 1 |
| cli-camd35.c | 2 | | srv-cs378x.c | 1 |
| cli-cs378x.c | 2 | | srv-mgcamd.c | 1 |
| cli-newcamd.c | 1 | | srv-newcamd.c | 1 |
| cli-radegast.c | 1 | | clustredcache.c | 1 |

أي أن الفلتر يسدّ **كل** المسارات: الخوادم المصدرية، الكاش، والكاش‑إكس.

منذ 1.10b البوابة العامّة `DCWFILTER CHECKSUM` موجودة. منذ 3.15 السطر
نفسه داخل البروفايل يتجاوز تلك البوابة وحدها. بلا سطر يبقى العامّ.
المفتاح الفارغ لا يمر من أي بروفايل.

---

## السبب 3 — `SID_FILTER` كان يسمّم البطاقة بشكل دائم — **أُغلق في TASK 3.13 (2026-09-26)**

هذا ما كان يحوّل رفضاً عابراً إلى شاشة سوداء مستمرة للباقة كلها.
العقوبة نفسها ما زالت تُطبَّق؛ ما أُغلق هو القفل الذي كان يمنع الارتداد.

`cli-cccam.c:250-258`:

```c
if (!acceptDCW(dcw)) {
	srv->ecmerrdcw ++;
#ifdef SID_FILTER
	if (cs && card) {
		cardsids_update( card, ecm->provid, ecm->sid, -1);   // <- عقوبة
		srv_cstatadd( srv, cs->id, 0 , 0);
	}
#endif
	ecm_setsrvflag(srv->ecm.request, srv->id, ECM_SRV_REPLY_FAIL);
	...
}
```

العقوبة تُطبَّق في كل البروتوكولات:
`cli-cccam.c:253`, `cli-camd35.c:177`, `cli-cs378x.c:246`,
`cli-newcamd.c:327`, `cli-radegast.c:159`, `cli-cccam.c:362`.

ومنزلق العقوبة في `cardsids_update` (`main.c:1774`). قبل 3.13 كان
الحارس `val>-100` يتخطى كل تحديث بعد الأرضية، فالنجاح الذي يصفّر رصيداً
سالباً لا يعمل ثانية. مع `DCW MAXFAILED` يستبعد `srvtab_arrange`
(`loadbalance.c:223`) البطاقة عند `val <= -maxfailedecm`، ورصيد متجمّد
عند `-100` يبقى مستبعداً حتى إعادة التشغيل.

منذ 3.13 الأرضية سقف لا قفل (`D43` / `§31`):

* الفشل الإضافي لا ينزل تحت `-100`.
* نجاح واحد عند الأرضية يعيد الرصيد إلى 0 — نفس التصفير الذي كان الفرع
  السالب يفعله فوق الأرضية. ليست قفزة إلى `-90`: ذلك سلوك جديد، ومع
  `MAXFAILED` أقل من 100 يبقى `-90` مستبعداً أصلاً.
* السقف `+100` لم يُمس.
* سطر واحد عند بلوغ الأرضية وسطر واحد عند التعافي (`!!! CARDSIDS:`).
* مواضع العقوبة في `cli-*.c` لم تتغيّر: CW مرفوضة ما زالت تُنقص الرصيد.

---

## السبب 4 — الخيار الموثّق `BAD-DCW` كان لا يعمل — **أُغلق في TASK 3.14 (2026-09-26)**

التوثيق في `index.php` يَعِد بـ:

```
BAD-DCW: FD FF FF FB FD FF FF FB FD FF FF FB FD FF FF FB
```

السطر يُقرأ في `config.c:2053` إلى `cfg.bad_dcw`. قبل 3.14 كان المشي
على هذه القائمة معلّقاً داخل `acceptDCW`، و`dcwfilter.c` (وفيه نسخة
عاملة من الفحص) غير مُدرَج في OBJECTS. المشغّل يكتب السطر والمفتاح
يصل إلى العميل.

منذ 3.14 (`D44` / `§32`):

- المفتاح المكتوب في السطر لا يُسلَّم، ويُعدّ `bad-dcw-list` حين تكون
  `DCW STATS` مفعّلة.
- مفتاح غير مكتوب يمر.
- بلا سطر لا يتغيّر شيء، ولا يُكتب سجل.
- إزالة السطر من الملف تمسح القائمة (`BAD-DCW: list cleared`).
- السقف 32 مفتاحاً. ما زاد يُذكر مرة في السجل ولا يُفرض.

الفحص في `dcw_on_badlist` (`dcw.c:192`)، لا بنقل `dcwfilter.c` إلى
البناء.

---

## تصحيح ذاتي لنقطتين قلتُهما أثناء التحليل

الأمانة تقتضي ذكرهما:

1. **قلت إن `DCW SWAP` و`DCW CHECK` معطّلان.** خطأ. تحقّقت من `Makefile`:
   `-DDCWSWAP` و`-DCHECK_NEXTDCW` موجودان في `OPTS` (الأسطر 2، 5، 8، 11)،
   فهما **يعملان** في البناء الافتراضي.
2. **قلت إنه لا توجد حماية من هجوم XOR 0xF0.** غير دقيق. الفحص يبيّن أن
   `checksumDCW` يرفض CW المزيّفة بـ XOR 0xF0 بشكل غير مباشر، لأنها تُفسد
   الـ checksum:

   ```
   -- the fake-CW attack described in the r107 changelog --
     [ ok ] XOR-0xF0 fake CW is rejected                   = 0
   ```

   ما ينقص حقاً هو رفض **صريح ومقصود** لهذا النمط مع تسجيله، لا الاعتماد على
   أثر جانبي.

---

## ملاحظات ثانوية (كود ميت / التباس صيانة)

* `ishalfnulledcw()` معرّفة في `dcw.c:214` ومُعلَنة في `dcw.h:25`
  و`ecmdata.h:147`، لكن **لا يستدعيها أحد** (`grep` يؤكد). المنطق الفعلي
  لـ NDS مكتوب يدوياً داخل `setdcw.c` (`dcwcheck_nds`, `setdcw.c:93`).
* وجود `dcw.c` و`dcwfilter.c` معاً بتعريفين مختلفين لـ `acceptDCW` مصيدة
  صيانة: من يعدّل `dcwfilter.c` يعتقد أنه يصلح الفلتر، بينما المُصرَّف هو
  `dcw.c`.

---

## ما الذي **ليس** سبباً (تم استبعاده بالقياس)

* CW بنصف مُصفَّر لـ NDS/Videoguard **ينجو** من الفلتر:

  ```
  info : NDS half-nulled CW -> checksum=1 ishalfnulledcw=1 acceptDCW=1
  [ ok ] NDS half-nulled CW survives acceptDCW            = 1
  ```

* CW صحيحة بـ checksum سليم تُقبل:

  ```
  [ ok ] valid CSA-checksum CW is accepted                = 1
  ```

---

## السبب 5 — فحص دورة الـ CW معطّل بالكامل في مسار الكاش

هذا السبب ينطبق مباشرة على حالة «شفرات خاطئة تصل للعملاء عبر الكاش».

الدالة `checkcycle()` في `clustredcache.c:541`:

```c
int checkcycle( uint8_t cw1cycle, uint8_t ecmtag, uint8_t cwcycle )
{
	if (cfg.cache.filter) {
		if (cw1cycle==0x80) { // cw1 cycle on tag=0x80
			if ( (ecmtag==0x80)&&(cwcycle==0) ) return 0;
			if ( (ecmtag==0x81)&&(cwcycle==1) ) return 0;
		}
		else if (cw1cycle==0x81) { // cw1 cycle on tag=0x81
			if ( (ecmtag==0x81)&&(cwcycle==0) ) return 0;
			if ( (ecmtag==0x80)&&(cwcycle==1) ) return 0;
		}
	}
	return 1;
}
```

وموضع استدعاؤها الوحيد `clustredcache.c:702`:

```c
if ( checkcycle( pcache->cw1cycle, pcache->tag, pcache->cwlist[i].cwcycle ) ) {
	ecm_setdcw( ecm, pcache->cwlist[i].cw, DCW_SOURCE_CACHE, pcache->cwlist[i].peerid );
}
```

**كلاهما داخل تعليق واحد**: `/*` في السطر 633 و`*/` في السطر 691.
تحقّقت بذلك بـ:

```
$ awk 'NR>=508 && NR<=695 { if ($0 ~ /^\/\*/ || $0 ~ /^\*\//) print NR": "$0 }' clustredcache.c
633:  /*
691:  */
```

ولذلك لا يظهر أي تحذير `implicit declaration` عند البناء، ولا رمز `checkcycle`
في الباينري — فالاستدعاء نفسه غير مُصرَّف.

**الأثر:** عندما يعيد الخادم استخدام CW مخزّنة من الكاش (`pipe_cache2ecm`
← `ecm_setdcw(..., DCW_SOURCE_CACHE, ...)`)، لا يوجد أي تدقيق بأن دورة تلك
الـ CW تطابق `cw1cycle` المعروف للقناة. الـ CW الصحيحة في الدورة الخاطئة
تُطبَّق في غير محلها فيظهر **عطب في الصورة أو شاشة سوداء**، رغم أن المفتاح
«صحيح».

الفحص الوحيد المتبقي هو `ecmdata.c:641-648`، لكنه داخل
`checkfreeze_checkECM` (كشف التجميد) ويعمل فقط عند توفّر
`lastdecode.counter>1` وتتابع ECM‑ين — أي أنه لا يغطي أول طلب ولا مسار الكاش
المباشر.

---

## ملخّص الأولوية حسب حالتك (proxy + كاش، CAID 1884/NLS)

| الأولوية | السبب | لماذا |
|---|---|---|
| 1 | السبب 5 (تعطّل `checkcycle`) | يفسّر «شفرات خاطئة» عبر الكاش تحديداً |
| 2 | السبب 3 (تسميم `SID_FILTER`) | يحوّل العطل العابر إلى دائم |
| 3 | ~~السبب 2 (checksum إلزامي)~~ — **أُغلق في 3.15** | باقة واحدة يمكنها إيقاف الفحص والباقي يبقى على العامّ |
| 4 | ~~السبب 1 (`isbadDCW`)~~ — **أُغلق في 3.15** | نفس السطر، البوابة `REPEAT` |
| 5 | ~~السبب 4 (`BAD-DCW` معطّل)~~ — **أُغلق في 3.14** | الحظر اليدوي يعمل الآن: المفتاح المكتوب لا يُسلَّم |
