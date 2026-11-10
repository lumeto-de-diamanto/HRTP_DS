"""Explicit production glyph inventory for TH01, including future screens.

Transcribed from the local ReC98 anniversary TH01 production string set
(excluding debug text), plus the fullwidth Latin/digit name-entry ranges.
Builds must not depend on a ReC98 checkout or silently succeed with only the
Latin ranges when the reference headers are missing. This is a character
selection, not font artwork; pixels still come from the user's font.bmp.
"""

PRODUCTION_CHARS = (
    '§“”…←→∞∴◆●★☆♀♂　'
    'あいぅうおかがくさざしすそたちっつでとなにのはばへまみもゃるれろをん'
    'ァアィイエキクゲコサザジスタッテトドナニノハパピフホマミムモュラリルンヴ・ー'
    '世丹九了人今仙伝似位使修倒元再刀前剣号合名回地堕墟士天太奇女媒子完実小巫帝帥師幽度廃強当得怪数断方明易星時最本東林業様樹檎死殿永炎点無猿獄王界異破社神祠禁称終経総者腐萬葉記説談超越逝違遠邪録鏡開陰陽難霊靈静非面類風高魔黄鼎'
    '！＃＆（）＊－．／０１２３４５６７８９？'
    'ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺ＿'
    'ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ～'
)
