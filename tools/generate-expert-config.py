#!/usr/bin/env python3
"""Build the reviewed deployment template from the tracked baseline.

The profile directives are copied without changing their values; only HTTP
management settings differ. Use --check to detect an out-of-sync template.
"""
from pathlib import Path
import argparse
import sys
ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tests'))
from cfgcheck import parse

source = ROOT / 'multics-r24-optimized.cfg'
target = ROOT / 'multics-r24-expert-reviewed.cfg'
global_values, profiles = parse(source)
assert len(profiles) == 72
header = '''# MultiCS r82a — قالب بروفايلات محافظ / 2026-10-29
# ينسخ كل قيم البروفايلات من multics-r24-optimized.cfg دون تغييرات تشغيلية.
# التقرير: docs/expert-profile-research-ar.md وdocs/profile-audit-ar.md.
# لا يثبت التوافق مع اشتراكات فعلية أو استقرار الصورة ولا يغني عن التحقق.
# HTTP متوقف عمدًا: ضع منفذ إدارة من 1024 إلى 65535 واسمًا وكلمة مرور
# قويين وفريدين عند التشغيل، واستخدم شبكة إدارة محمية؛ HTTP غير مشفر.
# أضف المستخدمين والمصادر المصرّح بها محليًا، ولا تضع أسرارًا في Git.
# الملف لا يضيف CAID 1861 إلى POLSAT؛ يحتاج قسمًا ومنفذًا وبيانات مثبتة.
# قيم SID/ECM والمهلات ومصادر الكاش تحتاج مطابقة مع التشغيل الفعلي.
# الأصل المرجعي: multics-r24-optimized.cfg؛ يُولّد هذا الملف بواسطة
# python3 tools/generate-expert-config.py بعد تعديل الأصل.

'''
rows = [header, '# الإعدادات المشتركة (السرعة والأمان يحددهما التشغيل الفعلي)\n']
for key, values in global_values.items():
    if key == 'HTTP PORT': values = ['0']
    elif key == 'HTTP USER': values = ['SET_MANAGEMENT_USER']
    elif key == 'HTTP PASS': values = ['SET_LONG_RANDOM_PASSWORD']
    for value in values: rows.append(key + ': ' + value + '\n')
for p in profiles:
    kv = p['kv']
    caid = int(kv['CAID'][-1].split('#')[0].strip(), 16)
    rows += ['\n[' + p['name'] + ']\n',
             '# CAID واحد لكل بروفايل؛ التسمية لا تثبت بيانات البث الحالية.\n']
    if caid >> 8 == 9:
        rows.append('# إعداد نصف CW وSWAP هنا وفق سياسة الكود؛ تحقّق من المصدر الفعلي.\n')
    if caid in (0x2600, 0x1010):
        rows.append('# إعداد مفتاح ثابت محلي؛ لا يتضمن الملف أي مفتاح.\n')
    if 'SID LIST' in kv or 'SID LIST !' in kv:
        rows.append('# قائمة SID أدناه تقيد القنوات؛ تحقق من تحديثها قبل الاعتماد.\n')
    if 'ECM LENGTH' in kv:
        rows.append('# قائمة أطوال ECM أدناه مقيدة حتى إذا كان ECM CHECK LENGTH: NO.\n')
    if 'ECM HEADERWLST' in kv:
        rows.append('# قائمة رؤوس ECM أدناه تقيد الطلبات؛ تحقق منها من البث.\n')
    if p['name'].startswith('POLSAT'):
        rows.append('# 1861 ليس مفعّلًا هنا: المفسّر يقرأ CAID واحدًا فقط.\n')
    if p['name'].startswith('OSN'):
        rows.append('# لا يدعم المفسّر ENABLE CAMD35/CS378X على مستوى القسم؛ القيم موروثة.\n')
    for key, values in kv.items():
        for value in values:
            # Remove source comment; numerical value is unchanged.
            rows.append(key + ': ' + value.split('#', 1)[0].strip() + '\n')
result = ''.join(rows)
parser = argparse.ArgumentParser()
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
if args.check:
    if not target.exists() or target.read_text(encoding='utf-8') != result:
        raise SystemExit('expert template is out of sync; regenerate it')
    print('PASS: template matches baseline and safe management overrides')
else:
    target.write_text(result, encoding='utf-8')
    print('generated', target.name)
