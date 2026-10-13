#include "Action.h"
#include "ConnectionManager.h"
#include "Logger.h"

Action::Action(ConnectionManager *connection, const QString &action, const QString &context)
  : mConnectionManager(connection), mAction(action), mContext(context)
{
}

Action::~Action()
{
}

QString Action::GetContext() const
{
    return mContext;
}

QString Action::GetAction() const
{
    return mAction;
}

ConnectionManager *Action::GetConnectionManager() const
{
    return mConnectionManager;
}

void Action::DidReceiveSettings(const QJsonObject &payload)
{
}

void Action::KeyDown(const QJsonObject &payload)
{
}

void Action::KeyUp(const QJsonObject &payload)
{
}

void Action::DialUp(const QJsonObject &payload)
{
}

void Action::DialDown(const QJsonObject &payload)
{
}

void Action::RotateClockwise(const QJsonObject &payload, const unsigned int ticks, const bool pressed)
{
}

void Action::RotateCounterClockwise(const QJsonObject &payload, const unsigned int ticks, const bool pressed)
{
}

void Action::SendToPlugin(const QJsonObject &payload)
{
}

void Action::WillAppear(const QJsonObject &payload)
{
}

void Action::WillDisappear(const QJsonObject &payload)
{
}

void Action::SetState(int state)
{
    GetConnectionManager()->SetState(state, mContext);
}

void Action::SetTitle(const QString &title, mSDKTarget target, int state)
{
    if (mFeedbackLayout.Contains("title") && SetFeedback({{"title", title}})) return;
    GetConnectionManager()->SetTitle(title, mContext, target, state);
}

void Action::SetImage(const QString &inBase64ImageString, mSDKTarget target, int state)
{
    mLastKnobImage.clear();
    GetConnectionManager()->SetImage(inBase64ImageString, mContext, target, state);
}

bool Action::SetFeedbackLayout(const QString &layoutPath, const QSize &outputSize)
{
    QString error;
    if (!mFeedbackLayout.Load(layoutPath, outputSize, error)) {
        Logger::LogToFile("[FeedbackLayout] " + GetContext() + ": " + error);
        return false;
    }
    mLastKnobImage.clear();
    GetConnectionManager()->SetTitle(QString(), mContext, mSDKTarget_HardwareAndSoftware);
    return SetFeedback({}, true);
}

bool Action::SetFeedbackLayout(const QJsonObject &layout, const QSize &outputSize, const QString &assetRoot)
{
    QString error;
    if (!mFeedbackLayout.Load(layout, outputSize, assetRoot, error)) {
        Logger::LogToFile("[FeedbackLayout] " + GetContext() + ": " + error);
        return false;
    }
    mLastKnobImage.clear();
    GetConnectionManager()->SetTitle(QString(), mContext, mSDKTarget_HardwareAndSoftware);
    return SetFeedback({}, true);
}

bool Action::SetFeedback(const QJsonObject &feedback, bool force)
{
    QString error;
    if (!mFeedbackLayout.IsLoaded()) {
        if (!mFeedbackLayout.LoadFromManifest(mAction, error)) {
            Logger::LogToFile("[FeedbackLayout] " + GetContext() + ": " + error);
            return false;
        }
        mLastKnobImage.clear();
        GetConnectionManager()->SetTitle(QString(), mContext, mSDKTarget_HardwareAndSoftware);
        force = true;
    }
    if (!mFeedbackLayout.Update(feedback, error)) {
        Logger::LogToFile("[FeedbackLayout] " + GetContext() + ": " + error);
        return false;
    }
    const QString image = mFeedbackLayout.Render(error);
    if (image.isEmpty()) {
        Logger::LogToFile("[FeedbackLayout] " + GetContext() + ": " + error);
        return false;
    }
    if (force || image != mLastKnobImage) {
        SetImage(image);
        mLastKnobImage = image;
    }
    return true;
}

void Action::SetSettings(const QJsonObject &inPayload)
{
    GetConnectionManager()->SetSettings(inPayload, mContext);
}

void Action::ShowAlert()
{
    GetConnectionManager()->ShowAlertForContext(mContext);
}

void Action::ShowOK()
{
    GetConnectionManager()->ShowOKForContext(mContext);
}

void Action::SendToPropertyInspector(const QJsonObject &inPayload)
{
    GetConnectionManager()->SendToPropertyInspector(mAction, mContext, inPayload);
}
