#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

class Config;

// One signed-in Grok CLI session, as stored in ~/.grok/auth.json.
// The access token is what Dictate sends to the speech API when no xAI key is set.
struct GrokCliSession {
    QString storageKey;
    QString accessToken;
    QString refreshToken;
    QString clientId;
    QString issuer;
    QString email;
    QString firstName;
    QDateTime expires;
    bool hasExpiry = false;
};

// First session object in a Grok CLI auth.json document. Does not touch the network.
bool grokCliSessionFromAuth(const QByteArray &json, GrokCliSession *session);

// The session in ~/.grok/auth.json. False when the file is missing or has no token.
bool grokCliLocalSession(GrokCliSession *session);

// xAI API key if one is configured, otherwise the signed-in Grok CLI token.
// Refreshes that token when it is about to expire. error is a sentence for the user.
QString grokBearerToken(const Config &config, QString *error = nullptr);
