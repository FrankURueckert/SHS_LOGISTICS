# -*- coding: utf-8 -*-
"""
Erzeugt:
1. SHS_Logistics_Handbuch_und_Projektbeschreibung.docx + .pdf (Umfassendes Technisches Handbuch & GUI-Manual)
2. SHS_Logistics_Kurzanleitung_und_Schnellstart.docx + .pdf (Kurzanleitung)
3. NSIS-Installer-Grafiken (Installer/SHS_Icon.ico, nsis_welcome.bmp, nsis_header.bmp, nsis_startseite_banner.bmp)
4. Kopiert alle PDF-Manuals in das Release_Standalone- und die Build-Verzeichnisse.

Wissenschaftliche Leitung & Software-Entwicklung:
Frank U. Rueckert (frank.rueckert@htwsaar.de)
htw saar - Hochschule fuer Technik und Wirtschaft des Saarlandes
"""

import os
import shutil
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn

# Zentrale Versionsnummer (identisch zu GUI/CMakeLists.txt und Installer/SHS_Logistics.nsi)
APP_VERSION = "0.8"


def set_cell_background(cell, hex_color: str):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), hex_color)
    tc_pr.append(shd)


def add_styled_heading(doc: Document, text: str, level: int = 1):
    h = doc.add_heading(text, level=level)
    for run in h.runs:
        run.font.name = "Segoe UI"
        if level == 1:
            run.font.size = Pt(15.5)
            run.font.color.rgb = RGBColor(0x3B, 0x46, 0x4E)  # SHS Stahl-Anthrazit
        elif level == 2:
            run.font.size = Pt(12.5)
            run.font.color.rgb = RGBColor(0xEF, 0x7D, 0x00)  # SHS Orange
        else:
            run.font.size = Pt(11.0)
            run.font.color.rgb = RGBColor(0x02, 0x84, 0xC7)  # SHS Stahlblau
    return h


def generate_nsis_artwork(repo_root: str):
    installer_dir = os.path.join(repo_root, "Installer")
    os.makedirs(installer_dir, exist_ok=True)

    logo_path = os.path.join(repo_root, "SHS_Logo.png")
    icon_png = os.path.join(repo_root, "SHSIcon.png")
    hero_jpg = os.path.join(repo_root, "startseite_img_de.jpg")
    hero_alias_jpg = os.path.join(repo_root, "startseite_imag_de.jpg")

    if os.path.exists(hero_jpg) and not os.path.exists(hero_alias_jpg):
        shutil.copyfile(hero_jpg, hero_alias_jpg)

    # 1. SHS_Icon.ico erzeugen
    src_ico = icon_png if os.path.exists(icon_png) else logo_path
    if os.path.exists(src_ico):
        im_ico = Image.open(src_ico).convert("RGBA")
        ico_out = os.path.join(installer_dir, "SHS_Icon.ico")
        im_ico.save(ico_out, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    # 2. nsis_welcome.bmp (164 x 314)
    welcome_w, welcome_h = 164, 314
    welcome_img = Image.new("RGB", (welcome_w, welcome_h), (59, 70, 78))
    draw_w = ImageDraw.Draw(welcome_img)

    draw_w.rectangle([0, 0, welcome_w, 86], fill=(244, 248, 250))
    draw_w.rectangle([0, 84, welcome_w, 88], fill=(239, 125, 0))

    if os.path.exists(logo_path):
        lg = Image.open(logo_path).convert("RGBA")
        lg.thumbnail((62, 62), Image.Resampling.LANCZOS)
        bg_patch = Image.new("RGBA", lg.size, (244, 248, 250, 255))
        bg_patch.paste(lg, (0, 0), lg)
        welcome_img.paste(bg_patch.convert("RGB"), (12, 12))

    draw_w.text((80, 24), "SHS", fill=(40, 50, 58))
    draw_w.text((80, 40), "LOGISTICS", fill=(239, 125, 0))
    draw_w.text((80, 56), "GmbH", fill=(92, 131, 156))

    if os.path.exists(hero_jpg):
        hero = Image.open(hero_jpg).convert("RGB")
        target_w = 152
        target_h = int(hero.height * (target_w / hero.width))
        hero_small = hero.resize((target_w, target_h), Image.Resampling.LANCZOS)
        draw_w.rectangle([4, 96, 159, 96 + target_h + 4], outline=(251, 213, 14), width=1)
        welcome_img.paste(hero_small, (6, 98))

        crop_box = (int(hero.width * 0.25), int(hero.height * 0.30), int(hero.width * 0.88), int(hero.height * 0.98))
        hero_crop = hero.crop(crop_box).resize((152, 96), Image.Resampling.LANCZOS)
        draw_w.rectangle([4, 192, 159, 291], outline=(92, 131, 156), width=1)
        welcome_img.paste(hero_crop, (6, 194))

    draw_w.rectangle([0, 296, welcome_w, welcome_h], fill=(43, 52, 58))
    draw_w.rectangle([0, 294, welcome_w, 297], fill=(251, 213, 14))
    draw_w.text((10, 300), "Partner [schafft Zukunft] Stahl", fill=(251, 213, 14))

    welcome_bmp = os.path.join(installer_dir, "nsis_welcome.bmp")
    welcome_img.save(welcome_bmp, format="BMP")

    # 3. nsis_header.bmp (150 x 57)
    hdr_w, hdr_h = 150, 57
    hdr_img = Image.new("RGB", (hdr_w, hdr_h), (240, 245, 248))
    draw_h = ImageDraw.Draw(hdr_img)
    if os.path.exists(hero_jpg):
        hero = Image.open(hero_jpg).convert("RGB")
        hero_hdr = hero.resize((92, 51), Image.Resampling.LANCZOS)
        hdr_img.paste(hero_hdr, (55, 3))
    if os.path.exists(logo_path):
        lg = Image.open(logo_path).convert("RGBA")
        lg.thumbnail((46, 46), Image.Resampling.LANCZOS)
        bg_p = Image.new("RGBA", lg.size, (240, 245, 248, 255))
        bg_p.paste(lg, (0, 0), lg)
        hdr_img.paste(bg_p.convert("RGB"), (5, 5))
    draw_h.rectangle([0, hdr_h - 3, hdr_w, hdr_h], fill=(239, 125, 0))
    hdr_bmp = os.path.join(installer_dir, "nsis_header.bmp")
    hdr_img.save(hdr_bmp, format="BMP")

    # 4. nsis_startseite_banner.bmp (440 x 210)
    ban_w, ban_h = 440, 210
    ban_img = Image.new("RGB", (ban_w, ban_h), (59, 70, 78))
    if os.path.exists(hero_jpg):
        hero = Image.open(hero_jpg).convert("RGB")
        hero_ban = hero.resize((ban_w - 6, ban_h - 6), Image.Resampling.LANCZOS)
        ban_img.paste(hero_ban, (3, 3))
    draw_b = ImageDraw.Draw(ban_img)
    draw_b.rectangle([0, 0, ban_w - 1, ban_h - 1], outline=(239, 125, 0), width=2)
    ban_bmp = os.path.join(installer_dir, "nsis_startseite_banner.bmp")
    ban_img.save(ban_bmp, format="BMP")


def convert_docx_to_pdf(docx_path: str, pdf_path: str):
    try:
        import win32com.client
        word = win32com.client.DispatchEx("Word.Application")
        word.Visible = False
        word.DisplayAlerts = 0
        doc = word.Documents.Open(os.path.abspath(docx_path), ReadOnly=True)
        doc.SaveAs(os.path.abspath(pdf_path), FileFormat=17)  # wdFormatPDF = 17
        doc.Close(False)
        word.Quit()
        print("PDF erfolgreich via Microsoft Word erzeugt:", pdf_path)
        return True
    except Exception as ex:
        print("Word-PDF-Konvertierung Hinweis:", ex)
        return False


def build_quickstart_manual(repo_root: str):
    logo_path = os.path.join(repo_root, "SHS_Logo.png")
    hero_jpg = os.path.join(repo_root, "startseite_img_de.jpg")
    img_vk = os.path.join(repo_root, "Docs", "Bilder", "preview_voelklingen.png")
    out_docx = os.path.join(repo_root, "SHS_Logistics_Kurzanleitung_und_Schnellstart.docx")
    out_pdf = os.path.join(repo_root, "SHS_Logistics_Kurzanleitung_und_Schnellstart.pdf")

    doc = Document()
    for section in doc.sections:
        section.top_margin = Inches(0.7)
        section.bottom_margin = Inches(0.7)
        section.left_margin = Inches(0.75)
        section.right_margin = Inches(0.75)

    normal_style = doc.styles["Normal"]
    normal_style.font.name = "Segoe UI"
    normal_style.font.size = Pt(10.0)

    if os.path.exists(logo_path):
        p_logo = doc.add_paragraph()
        p_logo.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_logo.add_run().add_picture(logo_path, width=Inches(1.0))

    p_t = doc.add_paragraph()
    p_t.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r1 = p_t.add_run("SHS LOGISTICS GmbH — Kurzanleitung & Schnellstart (Version " + APP_VERSION + ")\n")
    r1.font.size = Pt(18)
    r1.font.bold = True
    r1.font.color.rgb = RGBColor(0x3B, 0x46, 0x4E)
    r2 = p_t.add_run("Partner [schafft Zukunft] Stahl — Sofortüberblick für Leitstand & Disposition")
    r2.font.size = Pt(11.5)
    r2.font.bold = True
    r2.font.color.rgb = RGBColor(0xEF, 0x7D, 0x00)

    p_meta = doc.add_paragraph()
    p_meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_auth = p_meta.add_run("Wissenschaftliche Leitung & Software-Entwicklung: Frank U. Rückert | Kontakt: frank.rueckert@htwsaar.de | htw saar\n")
    r_auth.font.bold = True
    r_auth.font.size = Pt(9.5)
    r_auth.font.color.rgb = RGBColor(0x02, 0x84, 0xC7)

    if os.path.exists(hero_jpg):
        p_h = doc.add_paragraph()
        p_h.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_h.add_run().add_picture(hero_jpg, width=Inches(5.8))

    add_styled_heading(doc, "1. Programmstart", level=1)
    doc.add_paragraph(
        "Starten Sie die Anwendung über die Desktop-Verknüpfung bzw. den Startmenü-Eintrag 'SHS Logistics' "
        "(nach der Installation mit 'setup_SHS_Logistics.exe') oder direkt über 'SHS_Logistics.exe' im "
        "Release-Ordner. Nach wenigen Sekunden öffnet sich der interaktive 2D-Leitstand mit dem Werk Völklingen "
        "im Mittelpunkt."
    )

    add_styled_heading(doc, "2. Die wichtigsten Bedienelemente & Tastenkürzel auf einen Blick", level=1)
    sc_table = doc.add_table(rows=1, cols=3)
    sc_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    sc_table.style = "Table Grid"
    hdr = sc_table.rows[0].cells
    for i, txt in enumerate(["Taste / Menü", "Bedienelement im GUI", "Ausgeführte Aktion"]):
        hdr[i].text = txt
        set_cell_background(hdr[i], "48555F")
        for r in hdr[i].paragraphs[0].runs:
            r.font.bold = True
            r.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
            r.font.size = Pt(9.0)

    rows = [
        ("F1 / F2 / F3 / F4 / F5", "Obere Standort-Leiste", "Zoom auf Werk Völklingen (F1), Dillinger Hütte (F2), Burbach (F3), Neunkirchen (F4), Alle (F5)"),
        ("Leertaste (Space)", "Button 'Simulation Pause/Start'", "Pausiert oder startet die 2D-Zugsimulation und Be-/Entladung"),
        ("Mausrad / Linksklick", "2D-Kartenfenster (Mitte)", "Stufenlos zoomen, Karte verschieben, Waggon oder Quelle/Senke auswählen"),
        ("Tab 1: Züge & Waggons", "Buttons '+ Samms', '+ Rns-z', '🔥 + Torpedo'", "Züge dynamisch verlängern/verkürzen & 1420 °C Torpedowagen anhängen"),
        ("Tab 2: Güter & Quellen", "Buttons '⬆️ An Quelle aufnehmen' / '⬇️ Abladen'", "Stahlchargen anlegen, an Quellen beladen, an Senken abladen & Lok-Pool steuern"),
        ("F8 / F9 (Tab 4)", "Buttons '🎲 Zufalls-Pos.' / '📡 DB-API Ortung'", "Zufällige Testpositionierung (F8) oder DB-API / SAVVY Telematik-Abruf (F9)"),
        ("F10 / F11 (Menü Hilfe)", "Menü 'Hilfe' -> PDF-Manuals öffnen", "Öffnet direkt das Technische Handbuch (F10) oder diese Kurzanleitung (F11) als PDF")
    ]
    for row in rows:
        rc = sc_table.add_row().cells
        for i, val in enumerate(row):
            rc[i].text = val
            for r in rc[i].paragraphs[0].runs:
                r.font.size = Pt(9.0)

    if os.path.exists(img_vk):
        doc.add_paragraph()
        p_vk = doc.add_paragraph()
        p_vk.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_vk.add_run().add_picture(img_vk, width=Inches(6.0))

    doc.save(out_docx)
    print("Kurzanleitung (.docx) erstellt:", out_docx)
    convert_docx_to_pdf(out_docx, out_pdf)
    return out_pdf


def build_manual():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    generate_nsis_artwork(repo_root)

    logo_path = os.path.join(repo_root, "SHS_Logo.png")
    hero_jpg = os.path.join(repo_root, "startseite_img_de.jpg")
    img_vk = os.path.join(repo_root, "Docs", "Bilder", "preview_voelklingen.png")
    img_dl = os.path.join(repo_root, "Docs", "Bilder", "preview_dillingen.png")
    img_cor = os.path.join(repo_root, "Docs", "Bilder", "preview_corridor.png")
    img_goods = os.path.join(repo_root, "Docs", "Bilder", "preview_goods_tab.png")
    img_api = os.path.join(repo_root, "Docs", "Bilder", "preview_api_tab.png")
    out_docx = os.path.join(repo_root, "SHS_Logistics_Handbuch_und_Projektbeschreibung.docx")
    out_pdf = os.path.join(repo_root, "SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf")

    doc = Document()

    for section in doc.sections:
        section.top_margin = Inches(0.75)
        section.bottom_margin = Inches(0.75)
        section.left_margin = Inches(0.8)
        section.right_margin = Inches(0.8)

    normal_style = doc.styles["Normal"]
    normal_style.font.name = "Segoe UI"
    normal_style.font.size = Pt(10.0)

    # =========================================================================
    # TITELBLATT / HEADER MIT SHS-LOGO, STARTSEITEN-BILD & AUTOR FRANK U. RÜCKERT
    # =========================================================================
    if os.path.exists(logo_path):
        p_logo = doc.add_paragraph()
        p_logo.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run_logo = p_logo.add_run()
        run_logo.add_picture(logo_path, width=Inches(1.1))

    p_title = doc.add_paragraph()
    p_title.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_title = p_title.add_run("SHS LOGISTICS GmbH\n")
    r_title.font.name = "Segoe UI"
    r_title.font.size = Pt(22)
    r_title.font.bold = True
    r_title.font.color.rgb = RGBColor(0x3B, 0x46, 0x4E)

    r_sub = p_title.add_run("Werksbahn-, Waggon-, Güter-, Quellen/Senken- & Bahn-API-Leitsystem (C++ / Qt 6)\n")
    r_sub.font.name = "Segoe UI"
    r_sub.font.size = Pt(13.5)
    r_sub.font.bold = True
    r_sub.font.color.rgb = RGBColor(0xEF, 0x7D, 0x00)

    r_slogan = p_title.add_run("Partner [schafft Zukunft] Stahl — Ausführliches Technisches Handbuch & GUI-Bedienungsanleitung")
    r_slogan.font.name = "Segoe UI"
    r_slogan.font.size = Pt(11)
    r_slogan.font.italic = True
    r_slogan.font.color.rgb = RGBColor(0x5C, 0x83, 0x9C)

    if os.path.exists(hero_jpg):
        p_hero = doc.add_paragraph()
        p_hero.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_hero.add_run().add_picture(hero_jpg, width=Inches(5.6))

    p_meta = doc.add_paragraph()
    p_meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r_auth = p_meta.add_run("Wissenschaftliche Leitung & Software-Entwicklung: Frank U. Rückert\n")
    r_auth.font.bold = True
    r_auth.font.size = Pt(10.5)
    r_auth.font.color.rgb = RGBColor(0x02, 0x84, 0xC7)

    r_contact = p_meta.add_run("Kontakt: frank.rueckert@htwsaar.de  |  htw saar – Hochschule für Technik und Wirtschaft des Saarlandes\n"
                               f"Kooperationspartner: SHS Logistics GmbH (Stahl-Holding-Saar)  |  Software-Version: {APP_VERSION}  |  Stand: {datetime.now().strftime('%d.%m.%Y')}")
    r_contact.font.size = Pt(9.5)
    r_contact.font.color.rgb = RGBColor(0x64, 0x74, 0x8B)

    # =========================================================================
    # 1. PROJEKTÜBERBLICK & AUTORSCHAFT
    # =========================================================================
    add_styled_heading(doc, "1. Überblick & Autorschaft", level=1)
    doc.add_paragraph(
        "Das Software-System SHS_LOGISTICS (Version " + APP_VERSION + ") wurde unter der wissenschaftlichen Leitung und Entwicklung von "
        "Frank U. Rückert (htw saar – Hochschule für Technik und Wirtschaft des Saarlandes, frank.rueckert@htwsaar.de) "
        "für die SHS Logistics GmbH (100%ige Tochtergesellschaft der SHS – Stahl-Holding-Saar) realisiert. "
        "Das Tool bildet die saarländische Werksbahn- und Schwergutlogistik zwischen der Dillinger Hütte, "
        "den ROGESA Hochöfen und den drei Saarstahl-Standorten Völklingen, Burbach und Neunkirchen digital in "
        "einem interaktiven 2D-Leitsystem ab."
    )

    # =========================================================================
    # 2. THEORETISCHE & LOGISTISCHE GRUNDLAGEN DER SAAR-STAHLINDUSTRIE
    # =========================================================================
    add_styled_heading(doc, "2. Logistische Grundlagen der saarländischen Stahlindustrie", level=1)
    doc.add_paragraph(
        "Die saarländische Hüttenindustrie ist historisch und verfahrenstechnisch eng verzweigt. Das System "
        "SHS_LOGISTICS bildet die tatsächlichen Güter- und Transportkreisläufe detailgetreu nach:"
    )

    bullets_basics = [
        ("Flüssigroheisen-Transport bei 1420 °C (Dillingen -> Völklingen): ",
         "Die Roheisenerzeugung der ROGESA Hochöfen 4 & 5 erfolgt im Werk Dillingen. Das flüssige Eisen verlässt "
         "den Hochofenabstich mit ca. 1420 °C bis 1480 °C. Um ein vorzeitiges Einfrieren (Erstarrungsgrenze bei ca. "
         "1150 °C) zu verhindern, kommen hochisolierte Torpedopfannenwagen zum Einsatz. Der doppelkonische "
         "Mischbehälter besitzt eine dicke feuerfeste Ausmauerung aus Schamotte- und Tonerdesteinen. Der Temperaturverlust "
         "während der Bahnfahrt über die Saarstrecke (VzG 3230) beträgt lediglich ca. 8 bis 12 Kelvin pro Stunde. "
         "Im Werk Völklingen wird das flüssige Eisen direkt in die Roheisen-Entschwefelungsanlage und anschließend in "
         "die LD-Konverter (Linz-Donawitz-Verfahren) chargiert."),
        ("Brammentransport auf 6-Achser Samms-Wagen (Dillingen -> Völklingen / Hafen): ",
         "Stahlbrammen (Slabs) aus der Dillinger Stranggießanlage wiegen bis zu 30 Tonnen pro Einzelstück. Für ihren "
         "sicheren Transport werden Schwerlast-Flachwagen der Gattung Samms 489/709 eingesetzt. Mit zwei 3-achsigen "
         "Drehgestellen (6 Achsen) und einer Radsatzlast von 22,5 t (Streckenklasse D4) transportiert ein Samms-Wagen "
         "bis zu 92 Tonnen Nutzlast. Das Programm zeichnet zwei massive Brammenblöcke maßstäblich auf der Ladefläche."),
        ("Dillinger Grobbleche auf 4-Achser Rungenwagen Rns-z: ",
         "Das Dillinger Grobblechwalzwerk produziert weltweit führende Schwerbleche für Offshore-Windparks, Pipelines und "
         "den Brückenbau. Der Abtransport zum Hafen Dillingen oder in den Fernversand erfolgt auf 4-achsigen Rungenwagen "
         "(Rns-z 642). Gelbe Sicherungsrungen verhindern ein Verrutschen in Kurven; zusätzliche Spannbänder arretieren "
         "die Blechtafeln."),
        ("Stahlknüppel & Vorblöcke (Völklingen -> Drahtwerk Burbach & Walzwerk Neunkirchen): ",
         "Im Völklinger LD-Stahlwerk gegossene Vierkant-Knüppel (180x180 mm) werden per Bahn zum Drahtwerk Burbach "
         "transportiert und dort auf der Hochleistungs-Drahtstraße zu Walzdraht verarbeitet. Vorblöcke (Blooms) gehen an das "
         "Walzwerk Neunkirchen für Qualitäts- und Edelbaustähle sowie an die Saarschmiede GmbH für Turbinenwellen."),
        ("Aufgerollter Walzdraht (Coils) auf 4-Achser Spezialwagen: ",
         "Der in Burbach, Neunkirchen und Nauweiler Gewann gewalzte Draht wird zu Coils aufgerollt und auf speziellen "
         "4-Achser Coilwagen (Shimmns) mit Muldenböden transportiert. Die 2D-Darstellung zeigt 4 konzentrische Drahtrollen "
         "mit charakteristischem Innenloch.")
    ]
    for title_b, desc_b in bullets_basics:
        p = doc.add_paragraph(style="List Bullet")
        r_b1 = p.add_run(title_b)
        r_b1.bold = True
        p.add_run(desc_b)

    # =========================================================================
    # 3. MATHEMATISCHE & SOFTWARETECHNISCHE MODELLIERUNG
    # =========================================================================
    add_styled_heading(doc, "3. Mathematische und softwaretechnische Modellierung", level=1)

    add_styled_heading(doc, "3.1 Geodaten-Projektion (Web-Mercator EPSG:3857)", level=2)
    doc.add_paragraph(
        "Um reale Google-Maps-Kartenkacheln mit CAD-Vektoren (Gleise, Werkhallen, Straßen, Flüsse) und 2D-Zügen zu "
        "überlagern, implementiert RailMapWidget die sphärische Web-Mercator-Transformation:\n"
        "Für die geographische Länge lambda und Breite phi berechnen sich die normalisierten Weltkoordinaten x und y "
        "bei Zoomstufe z über:\n"
        "x = ((lambda + 180) / 360) * 256 * 2^z\n"
        "y = (0.5 - ln(tan(pi/4 + phi_rad/2)) / (2*pi)) * 256 * 2^z\n"
        "Der lokale Maßstab (Meter pro Pixel) variiert mit dem Breitengrad phi und beträgt bei z=15 in Völklingen "
        "(49.25° N) exakt mpp = (cos(phi) * 2 * pi * R_Erde) / (256 * 2^z) ca. 3.1 Meter pro Pixel."
    )

    add_styled_heading(doc, "3.2 Map-Matching & Track-Snapping Algorithmus", level=2)
    doc.add_paragraph(
        "Reale Telematikboxen (GNSS-Tracker an Güterwagen) besitzen zwischen massiven Stahlwerkshallen eine Streuung "
        "von ca. 1.5 bis 3.5 Metern (Multipath-Effekt). Das System führt daher bei jedem Ortungstelegramm ein "
        "automatisches Map-Matching durch:\n"
        "Für jedes Gleissegment zwischen Wegpunkt A und B wird das Lot vom gemessenen GPS-Punkt P gefällt:\n"
        "t = clamp( ((P - A) dot (B - A)) / |B - A|^2, 0.0, 1.0 )\n"
        "Der projizierte Punkt P_proj = A + t * (B - A) liefert die minimale Distanz zur Gleisachse. Der Zugverband "
        "wird mathematisch exakt auf diesen Gleismeter aufgeschnappt (Track-Snapping). Dies garantiert, dass Waggons "
        "stets präzise auf den Schienen laufen und niemals abseits der Gleise dargestellt werden."
    )

    add_styled_heading(doc, "3.3 Kinematik des Zugverbands & Drehgestell-Achsen", level=2)
    doc.add_paragraph(
        "Ausgehend von der Lokomotiv-Kopfposition d_head auf dem Gleis werden die Positionen aller Waggons "
        "entlang des Gleispolygons rückwärts iteriert:\n"
        "d_wagon_i = d_head - direction * sum_{j=1}^i (LueP_j + Kupplungsabstand)\n"
        "Über die Gleis-Abtastmethode sampleTrackAtDistance() werden für jeden Waggon dessen GPS-Koordinate sowie "
        "sein momentaner Tangentenwinkel (headingDeg) ermittelt. Bei Kurvenfahrten schwenkt der Wagenkasten "
        "tangential zur Schiene ein; die Drehgestell-Querachsen (4 Querachsen bei Rns-z/Shimmns bzw. 6 Querachsen bei Samms/Torpedo) "
        "werden senkrecht zur Gleisachse gezeichnet."
    )

    add_styled_heading(doc, "3.4 Der 4-Phasen-Logistikzyklus & Massenbilanzen", level=2)
    doc.add_paragraph(
        "Jeder Zugverband operiert als endlicher Zustandsautomat (Finite State Machine) mit folgenden Phasen:\n"
        "1. LoadingAtSource (Güter-Aufnahme an Quelle): Der Zug hält unter dem Kranportal der Quelle. Der Ladefortschritt "
        "steigt mit dt * Geschwindigkeit. Die Waggons füllen sich nacheinander von 'Leer' mit dem spezifischen Stahlprodukt. "
        "Der Lagerbestand der Quelle sinkt; die Tonnage-Aufnahmebilanz steigt.\n"
        "2. MovingLoadedToSink (Beladene Fahrt): Der Zug beschleunigt und fährt beladen zur Ziel-Senke.\n"
        "3. UnloadingAtSink (Güter-Abladung an Senke): Am Zielkranportal stoppt der Zug. Die Waggons entladen nacheinander "
        "zu Leerwagen (0 t). Die Senke verbucht den Materialeingang.\n"
        "4. MovingEmptyToSource (Leerzug-Rückfahrt): Der Zug kehrt leer zur Quelle zurück.\n"
        "5. ParkedWithoutLoco (Lok-Engpass): Steht im Lokomotiv-Pool keine freie Maschine zur Verfügung, verbleiben "
        "die beladenen Waggons als gesicherte Puffergruppe auf Abstellgleisen."
    )

    if os.path.exists(img_goods):
        p_img4 = doc.add_paragraph()
        p_img4.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_img4.add_run().add_picture(img_goods, width=Inches(6.5))
        p_cap4 = doc.add_paragraph(
            "Abbildung 1: Tab '2. Güter, Quellen & Senken' mit aktiver Güter-Aufnahme/Abladung, Güter-Objekten und Lokomotiv-Pool."
        )
        p_cap4.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_cap4.runs[0].font.size = Pt(9)
        p_cap4.runs[0].font.italic = True

    # =========================================================================
    # 4. WAGGON-KLASSIFIZIERUNG & FARB-MODI
    # =========================================================================
    add_styled_heading(doc, "4. Waggon-Klassifizierung, Geometrie & Optische Farbmodi", level=1)

    table = doc.add_table(rows=1, cols=5)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.style = "Table Grid"
    hdr_cells = table.rows[0].cells
    headers = ["Waggon-Bauart", "Kurzcode", "Achsen & Geometrie", "Kennfarbe", "Ladung & 2D-Darstellung"]
    for i, h_text in enumerate(headers):
        hdr_cells[i].text = h_text
        set_cell_background(hdr_cells[i], "48555F")
        for run in hdr_cells[i].paragraphs[0].runs:
            run.font.bold = True
            run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
            run.font.size = Pt(9.0)

    wagon_rows = [
        ("Samms 489/709 (Schwerlastwagen)", "Samms [6A]", "6 Achsen (2x 3-achsige Drehgestelle, LüP 16.4 m)", "SHS-Orange (#EF7D00)", "Stahlknüppel, Vorblöcke, Brammen, Schmiedeblöcke (~88-92 t)"),
        ("Rns-z 642 (Rungenwagen)", "Rns-z [4A]", "4 Achsen (2x 2-achsig, LüP 19.9 m) + gelbe Rungen", "SHS-Stahlblau (#0284C7)", "Dillinger Grobbleche (mit Zurrgurten), Stabstahl & Profile (~60-64 t)"),
        ("4-Achser Shimmns / Eaos", "4-Achser [4A]", "4 Achsen (2x 2-achsig, LüP 12.0 m)", "Smaragdgrün (#10B981)", "Aufgerollter Walzdraht / Coils (4 konzentrische Drahtrollen, ~56 t)"),
        ("Torpedopfannenwagen (Roheisen)", "Torpedo [6A]", "6 Achsen (2x 3-achsige Drehgestellbrücken + spindelförmiger Kessel, LüP 24.5 m)", "Karmesinrot (#E11D48)", "Flüssigroheisen 1420 °C (weiß-gelb-rot glühendes Einfüllauge im Kessel, ~98 t)")
    ]
    for row_data in wagon_rows:
        row_cells = table.add_row().cells
        for i, val in enumerate(row_data):
            row_cells[i].text = val
            for run in row_cells[i].paragraphs[0].runs:
                run.font.size = Pt(8.8)

    doc.add_paragraph()
    doc.add_paragraph(
        "Über das Auswahlfeld 'Optischer Farbmodus' (Tab 1) kann die Darstellung umgeschaltet werden:\n"
        "• Alle 4 Bauarten getrennt: Samms=Orange, Rns-z=Stahlblau, 4-Achser=Grün, Torpedo=Karmesinrot.\n"
        "• Fokus Bauart (Samms vs. Rns-z hervorheben): Die Standardwagen Samms und Rns-z leuchten kräftig hervor; Spezialwagen werden gedämpft dargestellt.\n"
        "• Strikt nach Achsen (4-Achser vs. 6-Achser): Alle 4-Achser werden einheitlich blau und alle 6-Achser orange eingefärbt."
    )

    # =========================================================================
    # 5. SCHRITT-FÜR-SCHRITT-BEDIENUNGSANLEITUNG FÜR DAS GUI
    # =========================================================================
    add_styled_heading(doc, "5. Schritt-für-Schritt-Bedienungsanleitung für das GUI", level=1)

    add_styled_heading(doc, "5.1 Hauptbildschirm, Schnellwahlleiste & Tastenkürzel", level=2)
    doc.add_paragraph(
        "Das GUI gliedert sich in eine obere Schnellwahlleiste, den linken Steuerbereich mit vier Tabs und die zentrale "
        "2D-Karte. Die wichtigsten Tastenkürzel lauten:\n"
        "• F1: Zoom auf Saarstahl Werk Völklingen (LD-Stahlwerk, Strangguss, Walzwerk, Saarschmiede)\n"
        "• F2: Zoom auf Dillinger Hütte & ROGESA (Hochöfen 4 & 5, Grobblechwalzwerk, Hafen Dillingen)\n"
        "• F3: Zoom auf Saarstahl Werk Burbach (Drahtwerk)\n"
        "• F4: Zoom auf Saarstahl Werk Neunkirchen (Walzwerk & Edelbaustahl)\n"
        "• F5: Gesamtübersicht des Saarland-Bahnkorridors (Dillingen <-> Völklingen <-> Burbach <-> Neunkirchen)\n"
        "• Leertaste: Start / Pause der gesamten 2D-Simulation und Be-/Entladevorgänge\n"
        "• F8: Zufalls-Testpositionierung aller Züge auf den Gleisen\n"
        "• F9: Sofortiger Abruf der Waggon-Positionen über die Bahn-API (DB Telematik / SAVVY)\n"
        "• F10: Öffnet direkt dieses Technische Handbuch als PDF\n"
        "• F11: Öffnet die Kurzanleitung & Schnellstart als PDF\n"
        "• Strg+P: Druckdialog für hochauflösenden Ausdruck oder PDF-Export der aktuellen Kartenansicht\n"
        "• Strg+Q: Anwendung beenden"
    )

    add_styled_heading(doc, "5.2 Tab 1 bedienen: '🚂 Züge & Waggons'", level=2)
    doc.add_paragraph(
        "1. Verband auswählen: Wählen Sie im Dropdown 'Verband wählen' einen Zug (z. B. SHS-VK-101, TORPEDO-DL-201 oder "
        "eine abgestellte Waggongruppe LAGER-VK-P01). Ein Doppelklick auf eine Waggonzeile oder Klick auf '🔍 Auf Waggon zoomen' zentriert die Karte direkt auf das Fahrzeug.\n"
        "2. Zug verlängern / verkürzen: Klicken Sie auf '➕ Samms [6-Achser] anhängen', '➕ Rns-z [4-Achser] anhängen', '➕ 4-Achser (Draht-Coils) anhängen' oder "
        "'🔥 + Torpedowagen (Flüssigeisen)', um sofort einen neuen Waggon hinten anzuhängen. Über '➖ Letzten Waggon abhängen' "
        "wird der letzte Wagen abgekuppelt. Mit dem Drehfeld 'Zuglänge (Waggons)' lässt sich jede Zuglänge zwischen 1 und 30 "
        "Wagen direkt einstellen.\n"
        "3. Waggonposition gezielt umbauen: Klicken Sie in der Waggontabelle oder direkt auf der Karte einen Waggon an. "
        "Wählen Sie darunter die gewünschte Bauart und das Produkt und klicken Sie auf 'Waggon & Gut setzen'.\n"
        "4. Lokomotive an- oder abkuppeln: Mit '🚂 Freie Lok ankuppeln (Start)' wird eine Lokomotive aus dem Pool an eine "
        "gelagerte Gruppe gekuppelt. Mit '🅿️ Lok abkuppeln (Waggons lagern)' wird der Zug als gelagerte Waggongruppe auf dem Gleis geparkt."
    )

    add_styled_heading(doc, "5.3 Tab 2 bedienen: '📦 Güter, Quellen & Senken'", level=2)
    doc.add_paragraph(
        "1. Lokomotiv-Pool verwalten: Das Drehfeld 'Verfügbare Lokomotiven im Pool' legt fest, wie viele Maschinen "
        "im Einsatz sein können. Bei einer Reduzierung werden überzählige Verbände automatisch auf Abstellgleisen gelagert.\n"
        "2. Neues Güter-Objekt erzeugen: Wählen Sie das Stahlprodukt (z. B. Flüssigroheisen 1420 °C, Dillinger Grobbleche, "
        "Stahlbrammen, Knüppel, Coils), die Tonnage (40 bis 1800 t), Quelle, Senke und Waggonklasse. Ein Klick auf "
        "'📦 Güter-Objekt erzeugen & an Quelle auf Waggons aufnehmen' berechnet automatisch die benötigten Waggons und weist den Transport zu.\n"
        "3. Manuelle Beladung / Abladung: Mit '⬆️ An Quelle aufnehmen (Beladen)' springt der ausgewählte Zug zu seiner "
        "Quelle und belädt alle Waggons. Mit '⬇️ An Senke abladen (Entladen)' wird die Fracht an der Senke gelöscht und der "
        "Senken-Lagerbestand erhöht."
    )

    add_styled_heading(doc, "5.4 Tab 3 bedienen: '🗺️ Karte & Standorte'", level=2)
    doc.add_paragraph(
        "Hier wählen Sie zwischen Hybrid-Karten (Google Maps + vereinfachtes Werk), reiner Schematik/CAD-Ansicht, "
        "Straßenkarte oder Satellitenbildern. Über Checkboxen lassen sich Werkhallen, Straßen, Flüsse, Gleisbezeichnungen, "
        "Waggon-Etiketten und die Größenverstärkung individuell schalten. Der Schieberegler 'Tempo' regelt die "
        "Geschwindigkeit stufenlos von 0.2x (Zeitlupe) bis 10.0x."
    )

    add_styled_heading(doc, "5.5 Tab 4 bedienen: '📡 Bahn-API & Test'", level=2)
    doc.add_paragraph(
        "1. Test-Zufallsverteilung: Über '🎲 Zufällige Positionen auf allen Gleisen' werden Züge sofort zufällig im Netz verteilt.\n"
        "2. Bahn-API Telematik-Schnittstelle: Zeigt den REST-Endpunkt, Client-ID und API-Key. Bei Klick auf "
        "'📡 Waggon-Positionen über Bahn-API abrufen' (oder bei aktivierter 5-Sekunden-Autosynchronisation) wird das "
        "vollständige JSON-Telegramm mit DS100-Betriebsstellen (SVK, SDL, SBU, SNK), VzG-Streckenkilometern, Luhn-10-UIC-Nummern, "
        "Roh-GNSS-Koordinaten und gesnappten Gleiskoordinaten ausgegeben."
    )

    if os.path.exists(img_api):
        p_img_api = doc.add_paragraph()
        p_img_api.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_img_api.add_run().add_picture(img_api, width=Inches(6.5))
        p_cap_api = doc.add_paragraph(
            "Abbildung 2: Tab '📡 Bahn-API & Test' mit Ril-100/DS100-Betriebsstellen, VzG-Streckenkilometern und JSON-Protokoll."
        )
        p_cap_api.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p_cap_api.runs[0].font.size = Pt(9)
        p_cap_api.runs[0].font.italic = True

    # =========================================================================
    # 6. INSTALLATION & SETUP MIT SETUP_SHS_LOGISTICS.EXE
    # =========================================================================
    add_styled_heading(doc, "6. Installation, Standalone-Release & Dateistruktur", level=1)
    doc.add_paragraph(
        "Für die Weitergabe an Kunden und Leitstände wurde ein vollständiger Nullsoft-NSIS-Installer "
        "(setup_SHS_Logistics.exe) erstellt:\n"
        "• Sauber codierter NSIS-Installationsdialog (ohne Zeichenmischung, Umlaute fehlerfrei über native NSIS-Tabellen).\n"
        "• Grafische Einbindung des offiziellen SHS-Logos und des Firmenmotivs startseite_img_de.jpg.\n"
        "• Installiert die Standalone-Release-Binärdatei SHS_Logistics.exe inklusive aller Qt-6.9-DLLs, Plugins, "
        "dem Offline-Kachel-Cache tile_cache sowie beiden PDF-Handbüchern.\n"
        "• Erzeugt Startmenü- und Desktop-Icons für das Leitsystem und direkten Zugriff auf die PDF-Manuals.\n"
        "• Vollständiger Windows-Uninstaller (Uninstall.exe)."
    )

    sc_table = doc.add_table(rows=1, cols=2)
    sc_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    sc_table.style = "Table Grid"
    sch = sc_table.rows[0].cells
    sch[0].text = "Datei / Skript"
    sch[1].text = "Bedeutung im Projekt SHS_LOGISTICS"
    for c in sch:
        set_cell_background(c, "48555F")
        for r in c.paragraphs[0].runs:
            r.font.bold = True
            r.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
            r.font.size = Pt(9.0)

    shortcuts = [
        ("setup_SHS_Logistics.exe", "Offizieller Nullsoft-NSIS-Installer zur schlüsselfertigen Installation"),
        ("Release_Standalone/", "Komplett eigenständiger, sofort lauffähiger Release-Ordner ohne Installer"),
        ("Build_SHS_Logistics_Cpp.bat", "Kompiliert Debug- und Release-Version via CMake + Ninja + Qt 6.9 MinGW 64-Bit"),
        ("Build_Installer.bat", "Aktualisiert PDF-Manuals und baut setup_SHS_Logistics.exe neu"),
        ("Run_SHS_Logistics_Cpp.bat", "Startet sofort die Release-Standalone-Anwendung"),
        ("Open_QtCreator.bat", "Öffnet das C++/Qt-Projekt direkt in Qt Creator 18"),
        ("SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf", "Ausführliches Technisches Handbuch & GUI-Manual"),
        ("SHS_Logistics_Kurzanleitung_und_Schnellstart.pdf", "Kompakte Kurzanleitung mit Tastenkürzeln & Sofortübersicht")
    ]
    for k, v in shortcuts:
        rc = sc_table.add_row().cells
        rc[0].text = k
        rc[1].text = v
        for cell in rc:
            for r in cell.paragraphs[0].runs:
                r.font.size = Pt(9.0)

    # =========================================================================
    # 7. KONTAKT, IMPRESSUM & SUPPORT
    # =========================================================================
    add_styled_heading(doc, "7. Wissenschaftliche Leitung, Impressum & Support", level=1)
    doc.add_paragraph(
        "Für wissenschaftliche, fachliche und softwaretechnische Rückfragen zum System SHS_LOGISTICS:\n\n"
        "Wissenschaftliche Leitung & Software-Entwicklung:\n"
        "Frank U. Rückert\n"
        "E-Mail: frank.rueckert@htwsaar.de\n"
        "htw saar – Hochschule für Technik und Wirtschaft des Saarlandes\n"
        "Fakultät für Ingenieurwissenschaften\n"
        "Goebenstraße 40, 66117 Saarbrücken\n\n"
        "Industriepartner & Betreiber:\n"
        "SHS Logistics GmbH (Stahl-Holding-Saar)\n"
        "Werkstraße 1, 66763 Dillingen/Saar\n"
        "Web: https://www.shs-logistics.de"
    )

    doc.save(out_docx)
    print(f"Word-Handbuch erfolgreich erstellt: {out_docx}")
    convert_docx_to_pdf(out_docx, out_pdf)

    quick_pdf = build_quickstart_manual(repo_root)

    target_dirs = [
        os.path.join(repo_root, "Release_Standalone"),
        os.path.join(repo_root, "Release_Standalone", "Manuals"),
        os.path.join(repo_root, "GUI", "build", "Desktop_Qt_6_9_0_MinGW_64_bit-Release"),
        os.path.join(repo_root, "GUI", "build", "Desktop_Qt_6_9_0_MinGW_64_bit-Debug"),
    ]
    manuals_to_copy = [out_pdf, quick_pdf, out_docx, os.path.join(repo_root, "SHS_Logistics_Dokumentation_und_Handbuch.md")]
    for td in target_dirs:
        os.makedirs(td, exist_ok=True)
        for mf in manuals_to_copy:
            if mf and os.path.exists(mf):
                shutil.copy2(mf, os.path.join(td, os.path.basename(mf)))
    print("Alle PDF-Manuals in die Release- und Build-Verzeichnisse kopiert.")


if __name__ == "__main__":
    build_manual()
