/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_autocomplete.h"

#include "ui/widgets/scroll_area.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/padding_wrap.h"
#include "support/support_templates.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/history_message.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Support {
namespace {

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent);

	using Question = details::TemplatesQuestion;
	void showRows(std::vector<Question> &&rows);

	std::pair<int, int> moveSelection(int delta);

	std::optional<Question> selected() const;

	auto activated() const {
		return _activated.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	struct Row {
		Question data;
		Text question = { st::windowMinWidth / 2 };
		Text keys = { st::windowMinWidth / 2 };
		Text answer = { st::windowMinWidth / 2 };
		int top = 0;
		int height = 0;
	};

	void prepareRow(Row &row);
	int resizeRowGetHeight(Row &row, int newWidth);
	void setSelected(int selected);

	std::vector<Row> _rows;
	int _selected = -1;
	int _pressed = -1;
	bool _selectByKeys = false;
	rpl::event_stream<> _activated;

};

int TextHeight(const Text &text, int available, int lines) {
	Expects(text.style() != nullptr);

	const auto st = text.style();
	const auto line = st->lineHeight ? st->lineHeight : st->font->height;
	return std::min(text.countHeight(available), lines * line);
};

Inner::Inner(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
}

void Inner::showRows(std::vector<Question> &&rows) {
	_rows.resize(0);
	_rows.reserve(rows.size());
	for (auto &row : rows) {
		_rows.push_back({ std::move(row) });
		auto &added = _rows.back();
		prepareRow(added);
	}
	resizeToWidth(width());
	update();
	_selected = _pressed = -1;
}

std::pair<int, int> Inner::moveSelection(int delta) {
	const auto selected = _selected + delta;
	if (selected >= 0 && selected < _rows.size()) {
		_selectByKeys = true;
		setSelected(selected);
		const auto top = _rows[_selected].top;
		return { top, top + _rows[_selected].height };
	}
	return { -1, -1 };
}

auto Inner::selected() const -> std::optional<Question> {
	if (_rows.empty()) {
		return std::nullopt;
	} else if (_selected < 0) {
		return _rows[0].data;
	}
	return _rows[_selected].data;
}

void Inner::prepareRow(Row &row) {
	row.question.setText(st::autocompleteRowTitle, row.data.question);
	row.keys.setText(
		st::autocompleteRowKeys,
		row.data.keys.join(qstr(", ")));
	row.answer.setText(st::autocompleteRowAnswer, row.data.value);
}

int Inner::resizeRowGetHeight(Row &row, int newWidth) {
	const auto available = newWidth
		- st::autocompleteRowPadding.left()
		- st::autocompleteRowPadding.right();
	return row.height = st::autocompleteRowPadding.top()
		+ TextHeight(row.question, available, 1)
		+ TextHeight(row.keys, available, 1)
		+ TextHeight(row.answer, available, 2)
		+ st::autocompleteRowPadding.bottom()
		+ st::lineWidth;
}

int Inner::resizeGetHeight(int newWidth) {
	auto top = 0;
	for (auto &row : _rows) {
		row.top = top;
		top += resizeRowGetHeight(row, newWidth);
	}
	return top ? (top - st::lineWidth) : (3 * st::mentionHeight);
}

void Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_rows.empty()) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect(),
			"Search by question, keys or value",
			style::al_center);
		return;
	}

	const auto clip = e->rect();
	const auto from = ranges::upper_bound(
		_rows,
		clip.y(),
		std::less<>(),
		[](const Row &row) { return row.top + row.height; });
	const auto till = ranges::lower_bound(
		_rows,
		clip.y() + clip.height(),
		std::less<>(),
		[](const Row &row) { return row.top; });
	if (from == end(_rows)) {
		return;
	}
	p.translate(0, from->top);
	const auto padding = st::autocompleteRowPadding;
	const auto available = width() - padding.left() - padding.right();
	auto top = padding.top();
	const auto drawText = [&](const Text &text, int lines) {
		text.drawLeftElided(
			p,
			padding.left(),
			top,
			available,
			width(),
			lines);
		top += TextHeight(text, available, lines);
	};
	for (auto i = from; i != till; ++i) {
		const auto over = (i - begin(_rows) == _selected);
		if (over) {
			p.fillRect(0, 0, width(), i->height, st::windowBgOver);
		}
		p.setPen(st::mentionNameFg);
		drawText(i->question, 1);
		p.setPen(over ? st::mentionFgOver : st::mentionFg);
		drawText(i->keys, 1);
		p.setPen(st::windowFg);
		drawText(i->answer, 2);

		p.translate(0, i->height);
		top = padding.top();

		if (i - begin(_rows) + 1 == _selected) {
			p.fillRect(
				0,
				-st::lineWidth,
				width(),
				st::lineWidth,
				st::windowBgOver);
		} else if (!over) {
			p.fillRect(
				padding.left(),
				-st::lineWidth,
				available,
				st::lineWidth,
				st::shadowFg);
		}
	}
}

void Inner::mouseMoveEvent(QMouseEvent *e) {
	static auto lastGlobalPos = QPoint();
	const auto moved = (e->globalPos() != lastGlobalPos);
	if (!moved && _selectByKeys) {
		return;
	}
	_selectByKeys = false;
	lastGlobalPos = e->globalPos();
	const auto i = ranges::upper_bound(
		_rows,
		e->pos().y(),
		std::less<>(),
		[](const Row &row) { return row.top + row.height; });
	setSelected((i == end(_rows)) ? -1 : (i - begin(_rows)));
}

void Inner::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void Inner::setSelected(int selected) {
	if (_selected != selected) {
		_selected = selected;
			update();
	}
}

void Inner::mousePressEvent(QMouseEvent *e) {
	_pressed = _selected;
}

void Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = base::take(_pressed);
	if (pressed == _selected && pressed >= 0) {
		_activated.fire({});
	}
}

AdminLog::OwnedItem GenerateCommentItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const Contact &data) {
	if (data.comment.isEmpty()) {
		return nullptr;
	}
	using Flag = MTPDmessage::Flag;
	const auto id = ServerMaxMsgId + (ServerMaxMsgId / 2);
	const auto flags = Flag::f_entities | Flag::f_from_id | Flag::f_out;
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = new HistoryMessage(
		history,
		id,
		flags,
		replyTo,
		viaBotId,
		unixtime(),
		Auth().userId(),
		QString(),
		TextWithEntities{ TextUtilities::Clean(data.comment) });
	return AdminLog::OwnedItem(delegate, item);
}

AdminLog::OwnedItem GenerateContactItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const Contact &data) {
	using Flag = MTPDmessage::Flag;
	const auto id = ServerMaxMsgId + (ServerMaxMsgId / 2) + 1;
	const auto flags = Flag::f_from_id | Flag::f_media | Flag::f_out;
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto message = MTP_message(
		MTP_flags(flags),
		MTP_int(id),
		MTP_int(Auth().userId()),
		peerToMTP(history->peer->id),
		MTPMessageFwdHeader(),
		MTP_int(viaBotId),
		MTP_int(replyTo),
		MTP_int(unixtime()),
		MTP_string(QString()),
		MTP_messageMediaContact(
			MTP_string(data.phone),
			MTP_string(data.firstName),
			MTP_string(data.lastName),
			MTP_string(QString()),
			MTP_int(0)),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(0),
		MTP_int(0),
		MTP_string(QString()),
		MTP_long(0));
	const auto item = new HistoryMessage(history, message.c_message());
	return AdminLog::OwnedItem(delegate, item);
}

} // namespace

Autocomplete::Autocomplete(QWidget *parent, not_null<AuthSession*> session)
: RpWidget(parent)
, _session(session) {
	setupContent();
}

void Autocomplete::activate() {
	_activate();
}

void Autocomplete::deactivate() {
	_deactivate();
}

void Autocomplete::setBoundings(QRect rect) {
	const auto maxHeight = int(4.5 * st::mentionHeight);
	const auto height = std::min(rect.height(), maxHeight);
	setGeometry(
		rect.x(),
		rect.y() + rect.height() - height,
		rect.width(),
		height);
}

rpl::producer<QString> Autocomplete::insertRequests() const {
	return _insertRequests.events();
}

rpl::producer<Contact> Autocomplete::shareContactRequests() const {
	return _shareContactRequests.events();
}

void Autocomplete::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Up) {
		_moveSelection(-1);
	} else if (e->key() == Qt::Key_Down) {
		_moveSelection(1);
	}
}

void Autocomplete::setupContent() {
	const auto inputWrap = Ui::CreateChild<Ui::PaddingWrap<Ui::InputField>>(
		this,
		object_ptr<Ui::InputField>(
			this,
			st::gifsSearchField,
			[] { return "Search for templates"; }),
		st::autocompleteSearchPadding);
	const auto input = inputWrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::mentionScroll);

	const auto inner = scroll->setOwnedWidget(object_ptr<Inner>(scroll));

	const auto submit = [=] {
		if (const auto question = inner->selected()) {
			submitValue(question->value);
		}
	};

	const auto refresh = [=] {
		inner->showRows(
			_session->supportTemplates()->query(input->getLastText()));
		scroll->scrollToY(0);
	};

	inner->activated() | rpl::start_with_next(submit, lifetime());
	connect(input, &Ui::InputField::blurred, [=] {
		App::CallDelayed(10, this, [=] {
			if (!input->hasFocus()) {
				deactivate();
			}
		});
	});
	connect(input, &Ui::InputField::cancelled, [=] { deactivate(); });
	connect(input, &Ui::InputField::changed, refresh);
	connect(input, &Ui::InputField::submitted, submit);
	input->customUpDown(true);

	_activate = [=] {
		input->setText(QString());
		show();
		input->setFocus();
	};
	_deactivate = [=] {
		hide();
	};
	_moveSelection = [=](int delta) {
		const auto range = inner->moveSelection(delta);
		if (range.second > range.first) {
			scroll->scrollToY(range.first, range.second);
		}
	};

	paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter p(this);
		p.fillRect(
			clip.intersected(QRect(0, st::lineWidth, width(), height())),
			st::mentionBg);
		p.fillRect(
			clip.intersected(QRect(0, 0, width(), st::lineWidth)),
			st::shadowFg);
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		inputWrap->resizeToWidth(size.width());
		inputWrap->moveToLeft(0, st::lineWidth, size.width());
		scroll->setGeometry(
			0,
			inputWrap->height(),
			size.width(),
			size.height() - inputWrap->height() - st::lineWidth);
		inner->resizeToWidth(size.width());
	}, lifetime());
}

void Autocomplete::submitValue(const QString &value) {
	const auto prefix = qstr("contact:");
	if (value.startsWith(prefix)) {
		const auto line = value.indexOf('\n');
		const auto text = (line > 0) ? value.mid(line + 1) : QString();
		const auto commented = !text.isEmpty();
		const auto contact = value.mid(
			prefix.size(),
			(line > 0) ? (line - prefix.size()) : -1);
		const auto parts = contact.split(' ', QString::SkipEmptyParts);
		if (parts.size() > 1) {
			const auto phone = parts[0];
			const auto firstName = parts[1];
			const auto lastName = (parts.size() > 2)
				? QStringList(parts.mid(2)).join(' ')
				: QString();
			_shareContactRequests.fire(Contact{
				text,
				phone,
				firstName,
				lastName });
		}
	} else {
		_insertRequests.fire_copy(value);
	}
}

ConfirmContactBox::ConfirmContactBox(
	QWidget*,
	not_null<History*> history,
	const Contact &data,
	Fn<void()> submit)
: _comment(GenerateCommentItem(this, history, data))
, _contact(GenerateContactItem(this, history, data))
, _submit(submit) {
}

void ConfirmContactBox::prepare() {
	setTitle([] { return "Confirmation"; });

	auto maxWidth = 0;
	if (_comment) {
		_comment->setAttachToNext(true);
		_contact->setAttachToPrevious(true);
		_comment->initDimensions();
		accumulate_max(maxWidth, _comment->maxWidth());
	}
	_contact->initDimensions();
	accumulate_max(maxWidth, _contact->maxWidth());
	maxWidth += st::boxPadding.left() + st::boxPadding.right();
	const auto width = snap(maxWidth, st::boxWidth, st::boxWideWidth);
	const auto available = width
		- st::boxPadding.left()
		- st::boxPadding.right();
	auto height = 0;
	if (_comment) {
		height += _comment->resizeGetHeight(available);
	}
	height += _contact->resizeGetHeight(available);
	setDimensions(width, height);
	_contact->initDimensions();

	addButton(langFactory(lng_send_button), [=] {
		const auto weak = make_weak(this);
		_submit();
		if (weak) {
			closeBox();
		}
	});
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
}

void ConfirmContactBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);

	const auto ms = getms();
	p.translate(st::boxPadding.left(), 0);
	if (_comment) {
		_comment->draw(p, rect(), TextSelection(), ms);
		p.translate(0, _comment->height());
	}
	_contact->draw(p, rect(), TextSelection(), ms);
}

HistoryView::Context ConfirmContactBox::elementContext() {
	return HistoryView::Context::ContactPreview;
}

std::unique_ptr<HistoryView::Element> ConfirmContactBox::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<HistoryView::Message>(this, message);
}

std::unique_ptr<HistoryView::Element> ConfirmContactBox::elementCreate(
		not_null<HistoryService*> message) {
	return std::make_unique<HistoryView::Service>(this, message);
}

bool ConfirmContactBox::elementUnderCursor(not_null<const Element*> view) {
	return false;
}

void ConfirmContactBox::elementAnimationAutoplayAsync(
	not_null<const Element*> element) {
}

TimeMs ConfirmContactBox::elementHighlightTime(
		not_null<const Element*> element) {
	return TimeMs();
}

bool ConfirmContactBox::elementInSelectionMode() {
	return false;
}

} // namespace Support
