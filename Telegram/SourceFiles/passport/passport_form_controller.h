/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class BoxContent;

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

struct FormRequest {
	FormRequest(
		UserId botId,
		const QStringList &scope,
		const QString &callbackUrl,
		const QString &publicKey);

	UserId botId;
	QStringList scope;
	QString callbackUrl;
	QString publicKey;

};

struct IdentityData;

class FormController : private MTP::Sender {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();

	void submitPassword(const QString &password);
	rpl::producer<QString> passwordError() const;
	QString passwordHint() const;

	rpl::producer<> secretReadyEvents() const;

	QString defaultEmail() const;
	QString defaultPhoneNumber() const;

	void fillRows(
		base::lambda<void(
			QString title,
			QString description,
			bool ready)> callback);
	void editField(int index);

	void saveFieldIdentity(int index, const IdentityData &data);

private:
	struct File {
		uint64 id = 0;
		uint64 accessHash = 0;
		int32 size = 0;
		int32 dcId = 0;
		QByteArray fileHash;
	};
	struct Value {
		QString name;

		QByteArray data;
		QByteArray dataHash;
		QByteArray dataSecret;
		std::map<QString, QString> values;

		QString text;
		QByteArray textHash;

		std::vector<File> files;
		QByteArray filesHash;
		QByteArray filesSecret;
	};
	struct Field {
		enum class Type {
			Identity,
			Address,
			Phone,
			Email,
		};
		explicit Field(Type type);

		Type type;
		Value data;
		base::optional<Value> document;
	};
	struct Form {
		bool requestWrite = false;
		std::vector<Field> fields;
	};
	struct PasswordSettings {
		QByteArray salt;
		QByteArray newSalt;
		QString hint;
		QString unconfirmedPattern;
		bool hasRecovery = false;
	};
	Value convertValue(const MTPSecureValue &value) const;

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const RPCError &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();

	void passwordDone(const MTPaccount_Password &result);
	void passwordFail(const RPCError &error);
	void parsePassword(const MTPDaccount_noPassword &settings);
	void parsePassword(const MTPDaccount_password &settings);

	IdentityData fieldDataIdentity(const Field &field) const;

	std::map<QString, QString> fillData(const Value &from) const;
	void saveData(int index);
	void generateSecret(base::lambda<void()> callback);

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;
	QString _origin;

	mtpRequestId _formRequestId = 0;
	mtpRequestId _passwordRequestId = 0;
	mtpRequestId _passwordCheckRequestId = 0;

	PasswordSettings _password;
	Form _form;

	base::byte_vector _passwordHashForSecret;
	base::byte_vector _passwordHashForAuth;
	base::byte_vector _secret;
	mtpRequestId _saveSecretRequestId = 0;
	QString _passwordEmail;
	rpl::event_stream<> _secretReady;
	rpl::event_stream<QString> _passwordError;

	QPointer<BoxContent> _editBox;

};

} // namespace Passport
