#include "emoji.h"

#include <QHash>

namespace {

QString flagFromCode(const QString &code)
{
    if (code.size() != 2)
        return {};
    char32_t cps[2];
    for (int i = 0; i < 2; ++i) {
        const QChar ch = code.at(i).toUpper();
        if (ch < QLatin1Char('A') || ch > QLatin1Char('Z'))
            return {};
        cps[i] = char32_t(0x1F1E6u + uint(ch.unicode() - QLatin1Char('A').unicode()));
    }
    return QString::fromUcs4(cps, 2);
}

QString normKey(QString name)
{
    name = name.toLower();
    name.replace(QLatin1Char(' '), QLatin1Char('-'));
    name.remove(QLatin1Char('\''));
    name.remove(QLatin1Char('.'));
    name.remove(QLatin1Char(','));
    return name;
}

const QVector<Glyph> &emojiTable()
{
    static const QVector<Glyph> table = [] {
        QVector<Glyph> out;
        const char *rows[] = {
            "😀 grin",        "😃 smiley",       "😄 smile",        "😁 grin-beam",
            "😆 laughing",    "😅 sweat-smile",  "🤣 rofl",         "😂 joy",
            "🙂 slight-smile","😉 wink",         "😊 blush",        "😇 innocent",
            "🥰 love",        "😍 heart-eyes",   "😘 kiss",         "😗 kissing",
            "😋 yum",         "😛 tongue",       "😜 wink-tongue",  "🤪 zany",
            "😎 sunglasses",  "🤓 nerd",         "🧐 monocle",      "🤠 cowboy",
            "🥳 party",       "😏 smirk",        "😒 unamused",     "🙄 eye-roll",
            "😬 grimace",     "😴 sleeping",     "🤤 drool",        "😪 sleepy",
            "😷 mask",        "🤒 sick",         "🤕 bandage",      "🤢 nauseated",
            "🥵 hot",         "🥶 cold",         "😵 dizzy",        "🤯 exploding-head",
            "🤠 cowboy",      "😎 cool",         "🤓 nerd-face",    "😕 confused",
            "😟 worried",     "🙁 frown",        "😮 open-mouth",   "😯 hushed",
            "😲 astonished",  "😳 flushed",      "🥺 pleading",     "🥹 holding-tears",
            "😢 cry",         "😭 sob",          "😱 scream",       "😨 fearful",
            "😰 anxious",     "😥 sad-relief",   "😓 downcast",     "🤗 hug",
            "🤔 thinking",    "🫡 salute",       "🤫 shush",        "🤭 giggle",
            "🤥 lying",       "😐 neutral",      "😑 expressionless","😶 no-mouth",
            "🙄 roll",        "😈 imp",          "👻 ghost",        "💀 skull",
            "☠️ skull-cross", "👽 alien",        "🤖 robot",        "💩 poop",
            "👋 wave",        "🤚 raised-hand",  "✋ hand",         "👌 ok",
            "✌️ peace",       "🤞 fingers-crossed","🤟 love-you",  "🤘 horns",
            "👍 thumbsup",    "👎 thumbsdown",   "👏 clap",         "🙌 raised-hands",
            "🙏 pray",        "✍️ writing",      "💪 muscle",       "👀 eyes",
            "🧠 brain",       "🫀 heart-organ",  "💋 kiss-mark",    "💘 heart-arrow",
            "❤️ heart",       "🧡 orange-heart", "💛 yellow-heart", "💚 green-heart",
            "💙 blue-heart",  "💜 purple-heart", "🖤 black-heart",  "🤍 white-heart",
            "💔 broken-heart","❣️ heart-exclaim","💕 two-hearts",  "💖 sparkling-heart",
            "✨ sparkles",    "⭐ star",         "🌟 glow-star",    "🔥 fire",
            "💥 boom",        "💫 dizzy-star",   "💦 sweat",        "💨 dash",
            "🎉 tada",        "🎊 confetti",     "🎁 gift",         "🏆 trophy",
            "🥇 gold",        "🎯 target",       "📌 pin",          "📎 paperclip",
            "📝 memo",        "✏️ pencil",       "📚 books",         "📖 book",
            "📰 newspaper",   "🔗 link",         "💡 idea",         "🔔 bell",
            "⏰ clock",       "📅 calendar",     "💻 laptop",       "🖥️ desktop",
            "⌨️ keyboard",    "🖱️ mouse",        "📱 phone",        "📷 camera",
            "🎥 movie",       "🎮 game",         "🕹️ joystick",     "🎲 dice",
            "🎵 music",       "🎶 notes",        "🎤 mic",          "🎧 headphones",
            "☕ coffee",      "🍵 tea",          "🍺 beer",         "🍷 wine",
            "🍕 pizza",       "🍔 burger",       "🍟 fries",        "🍩 donut",
            "🍪 cookie",      "🎂 cake",         "🍎 apple",        "🌙 moon",
            "☀️ sun",         "🌤️ sun-cloud",   "⛅ partly",       "🌧️ rain",
            "⛈️ storm",       "❄️ snowflake",    "🌈 rainbow",      "⚡ zap",
            "🌊 wave-water",  "🌸 blossom",      "🌹 rose",         "🌻 sunflower",
            "🍀 clover",      "🌲 tree",         "🐱 cat",          "🐶 dog",
            "🦊 fox",         "🐸 frog",         "🦄 unicorn",      "🐝 bee",
            "🦋 butterfly",   "🐢 turtle",       "🦉 owl",          "🐧 penguin",
            "🦆 duck",        "🐦 bird",         "🐾 paws",         "🏠 house",
            "🏡 home",        "🏰 castle",       "🚗 car",          "✈️ airplane",
            "🚀 rocket",      "🚂 train",        "🚲 bike",         "⛵ sailboat",
            "🗺️ map",         "🧭 compass",      "⛺ tent",         "🧳 luggage",
            "✅ check",       "❌ cross",        "⚠️ warning",      "❗ exclaim",
            "❓ question",    "💯 hundred",      "🆕 new",          "🆗 ok-button",
            "🔴 red-circle",  "🟠 orange-circle","🟡 yellow-circle","🟢 green-circle",
            "🔵 blue-circle", "🟣 purple-circle","⚫ black-circle", "⚪ white-circle",
            "⬛ black-square","⬜ white-square", "🔶 diamond",      "🔸 small-diamond",
            "💬 speech",      "💭 thought",      "🗯️ anger",        "♻️ recycle",
            "🔒 lock",        "🔓 unlock",       "🔑 key",          "🛠️ tools",
            "⚙️ gear",        "🧪 test-tube",    "🔬 microscope",   "🔭 telescope",
            "🧙 witch",       "🧙‍♀️ witch-woman","🧙‍♂️ wizard",     "🧚 fairy",
            "🧛 vampire",     "🧟 zombie",       "🧞 genie",        "🧜 mermaid",
            "🦸 hero",        "🦹 villain",      "🧑 person",       "👩 woman",
            "👨 man",         "👶 baby",         "🧒 child",        "🧓 older",
            "💅 nails",       "👗 dress",        "👠 shoe",         "🎩 hat",
            "👑 crown",       "💍 ring",         "💎 gem",          "🪙 coin",
            "💰 money",       "📈 chart-up",     "📉 chart-down",   "📊 bar-chart",
            "🧾 receipt",     "📬 mail",         "✉️ envelope",     "🗳️ ballot",
            "🏛️ bank",        "⚖️ scales",       "⚔️ swords",       "🛡️ shield",
            "🏳️ white-flag",  "🏴 black-flag",   "🚩 flag",         "🏳️‍🌈 rainbow-flag",
        };
        out.reserve(int(sizeof(rows) / sizeof(rows[0])));
        for (const char *row : rows) {
            const QString line = QString::fromUtf8(row);
            const int sp = line.indexOf(QLatin1Char(' '));
            if (sp > 0)
                out.append({line.left(sp), line.mid(sp + 1)});
        }
        return out;
    }();
    return table;
}

const QVector<Glyph> &flagTable()
{
    static const QVector<Glyph> table = [] {
        const char *rows[] = {
            "AD Andorra", "AE United Arab Emirates", "AF Afghanistan", "AG Antigua and Barbuda",
            "AL Albania", "AM Armenia", "AO Angola", "AR Argentina", "AT Austria", "AU Australia",
            "AZ Azerbaijan", "BA Bosnia and Herzegovina", "BB Barbados", "BD Bangladesh", "BE Belgium",
            "BF Burkina Faso", "BG Bulgaria", "BH Bahrain", "BI Burundi", "BJ Benin", "BN Brunei",
            "BO Bolivia", "BR Brazil", "BS Bahamas", "BT Bhutan", "BW Botswana", "BY Belarus",
            "BZ Belize", "CA Canada", "CD Congo Kinshasa", "CF Central African Republic", "CG Congo",
            "CH Switzerland", "CI Ivory Coast", "CL Chile", "CM Cameroon", "CN China", "CO Colombia",
            "CR Costa Rica", "CU Cuba", "CV Cape Verde", "CY Cyprus", "CZ Czechia", "DE Germany",
            "DJ Djibouti", "DK Denmark", "DM Dominica", "DO Dominican Republic", "DZ Algeria",
            "EC Ecuador", "EE Estonia", "EG Egypt", "ER Eritrea", "ES Spain", "ET Ethiopia",
            "FI Finland", "FJ Fiji", "FM Micronesia", "FR France", "GA Gabon", "GB United Kingdom",
            "GD Grenada", "GE Georgia", "GH Ghana", "GM Gambia", "GN Guinea", "GQ Equatorial Guinea",
            "GR Greece", "GT Guatemala", "GW Guinea-Bissau", "GY Guyana", "HN Honduras", "HR Croatia",
            "HT Haiti", "HU Hungary", "ID Indonesia", "IE Ireland", "IL Israel", "IN India",
            "IQ Iraq", "IR Iran", "IS Iceland", "IT Italy", "JM Jamaica", "JO Jordan", "JP Japan",
            "KE Kenya", "KG Kyrgyzstan", "KH Cambodia", "KI Kiribati", "KM Comoros", "KN Saint Kitts",
            "KP North Korea", "KR South Korea", "KW Kuwait", "KZ Kazakhstan", "LA Laos", "LB Lebanon",
            "LC Saint Lucia", "LI Liechtenstein", "LK Sri Lanka", "LR Liberia", "LS Lesotho",
            "LT Lithuania", "LU Luxembourg", "LV Latvia", "LY Libya", "MA Morocco", "MC Monaco",
            "MD Moldova", "ME Montenegro", "MG Madagascar", "MH Marshall Islands", "MK North Macedonia",
            "ML Mali", "MM Myanmar", "MN Mongolia", "MR Mauritania", "MT Malta", "MU Mauritius",
            "MV Maldives", "MW Malawi", "MX Mexico", "MY Malaysia", "MZ Mozambique", "NA Namibia",
            "NE Niger", "NG Nigeria", "NI Nicaragua", "NL Netherlands", "NO Norway", "NP Nepal",
            "NR Nauru", "NZ New Zealand", "OM Oman", "PA Panama", "PE Peru", "PG Papua New Guinea",
            "PH Philippines", "PK Pakistan", "PL Poland", "PS Palestine", "PT Portugal", "PW Palau",
            "PY Paraguay", "QA Qatar", "RO Romania", "RS Serbia", "RU Russia", "RW Rwanda",
            "SA Saudi Arabia", "SB Solomon Islands", "SC Seychelles", "SD Sudan", "SE Sweden",
            "SG Singapore", "SI Slovenia", "SK Slovakia", "SL Sierra Leone", "SM San Marino",
            "SN Senegal", "SO Somalia", "SR Suriname", "SS South Sudan", "ST Sao Tome and Principe",
            "SV El Salvador", "SY Syria", "SZ Eswatini", "TD Chad", "TG Togo", "TH Thailand",
            "TJ Tajikistan", "TL Timor-Leste", "TM Turkmenistan", "TN Tunisia", "TO Tonga",
            "TR Turkey", "TT Trinidad and Tobago", "TV Tuvalu", "TW Taiwan", "TZ Tanzania",
            "UA Ukraine", "UG Uganda", "US United States", "UY Uruguay", "UZ Uzbekistan",
            "VA Vatican", "VC Saint Vincent", "VE Venezuela", "VN Vietnam", "VU Vanuatu",
            "WS Samoa", "XK Kosovo", "YE Yemen", "ZA South Africa", "ZM Zambia", "ZW Zimbabwe",
            "EU European Union", "UN United Nations",
        };
        QVector<Glyph> out;
        out.reserve(int(sizeof(rows) / sizeof(rows[0])));
        for (const char *row : rows) {
            const QString line = QString::fromUtf8(row);
            const int sp = line.indexOf(QLatin1Char(' '));
            if (sp != 2)
                continue;
            const QString code = line.left(2);
            const QString flag = flagFromCode(code);
            if (!flag.isEmpty())
                out.append({flag, line.mid(3) + QStringLiteral("  ") + code});
        }
        return out;
    }();
    return table;
}

const QHash<QString, QString> &shortcodes()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> out;
        for (const Glyph &g : emojiTable()) {
            out.insert(normKey(g.name), g.emoji);
            // "thumbsup" already has no hyphen. Also accept a few aliases.
        }
        out.insert(QStringLiteral("thumbsup"), QString::fromUtf8("👍"));
        out.insert(QStringLiteral("+1"), QString::fromUtf8("👍"));
        out.insert(QStringLiteral("-1"), QString::fromUtf8("👎"));
        out.insert(QStringLiteral("thumbsdown"), QString::fromUtf8("👎"));
        for (const Glyph &g : flagTable()) {
            const QString name = g.name.section(QStringLiteral("  "), 0, 0);
            const QString code = g.name.section(QStringLiteral("  "), 1, 1).toLower();
            out.insert(normKey(name), g.emoji);
            if (code.size() == 2)
                out.insert(QStringLiteral("flag-") + code, g.emoji);
        }
        return out;
    }();
    return map;
}

} // namespace

QVector<Glyph> emojiGlyphs()
{
    return emojiTable();
}

QVector<Glyph> flagGlyphs()
{
    return flagTable();
}

QString emojiForShortcode(const QString &code)
{
    QString key = code.trimmed().toLower();
    if (key.startsWith(QLatin1Char(':')) && key.endsWith(QLatin1Char(':')) && key.size() > 2)
        key = key.mid(1, key.size() - 2);
    if (key.startsWith(QLatin1String("flag-")) && key.size() == 7)
        return flagFromCode(key.mid(5));
    return shortcodes().value(key);
}
