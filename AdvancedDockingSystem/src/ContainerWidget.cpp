#include "ads/ContainerWidget.h"
#include "ads/Internal.h"
#include "ads/SectionTitleWidget.h"
#include "ads/SectionContentWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSplitter>
#include <QDataStream>
#include <QtGlobal>

ADS_NAMESPACE_BEGIN

// Static Helper //////////////////////////////////////////////////////

static QSplitter* newSplitter(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = 0)
{
	QSplitter* s = new QSplitter(orientation, parent);
	s->setChildrenCollapsible(false);
	s->setOpaqueResize(false);
	return s;
}

///////////////////////////////////////////////////////////////////////

ContainerWidget::ContainerWidget(QWidget *parent) :
	QFrame(parent),
	_mainLayout(NULL),
	_orientation(Qt::Horizontal),
	_splitter(NULL)
{
	_mainLayout = new QGridLayout();
	_mainLayout->setContentsMargins(9, 9, 9, 9);
	_mainLayout->setSpacing(0);
	setLayout(_mainLayout);
}

Qt::Orientation ContainerWidget::orientation() const
{
	return _orientation;
}

void ContainerWidget::setOrientation(Qt::Orientation orientation)
{
	if (_orientation != orientation)
	{
		_orientation = orientation;
		emit orientationChanged();
	}
}

SectionWidget* ContainerWidget::addSectionContent(const SectionContent::RefPtr& sc, SectionWidget* sw, DropArea area)
{
	if (!sw)
	{
		if (_sections.isEmpty())
		{	// Create default section
			sw = newSectionWidget();
			addSection(sw);
		}
		else if (area == CenterDropArea)
			// Use existing default section
			sw = _sections.first();
	}

	// Drop it based on "area"
	InternalContentData data;
	data.content = sc;
	data.titleWidget = new SectionTitleWidget(sc, NULL);
	data.contentWidget = new SectionContentWidget(sc, NULL);
	return dropContent(data, sw, area, false);
}

QMenu* ContainerWidget::createContextMenu() const
{
	QMenu* m = new QMenu(NULL);

	// Contents of SectionWidgets
	for (int i = 0; i < _sections.size(); ++i)
	{
		SectionWidget* sw = _sections.at(i);
		QList<SectionContent::RefPtr> contents = sw->contents();
		foreach (const SectionContent::RefPtr& c, contents)
		{
			QAction* a = m->addAction(QIcon(), c->uniqueName());
			a->setProperty("uid", c->uid());
			a->setProperty("type", "section");
			a->setCheckable(true);
			a->setChecked(c->titleWidget()->isVisible());
#if QT_VERSION >= 0x050000
			QObject::connect(a, &QAction::toggled, this, &ContainerWidget::onActionToggleSectionContentVisibility);
#else
			QObject::connect(a, SIGNAL(toggled(bool)), this, SLOT(onActionToggleSectionContentVisibility(bool)));
#endif
		}
	}

	// Contents of FloatingWidgets
	if (_floatings.size())
	{
		if (m->actions().size())
			m->addSeparator();
		for (int i = 0; i < _floatings.size(); ++i)
		{
			FloatingWidget* fw = _floatings.at(i);
			SectionContent::RefPtr c = fw->content();
			QAction* a = m->addAction(QIcon(), c->uniqueName());
			a->setProperty("uid", c->uid());
			a->setProperty("type", "floating");
			a->setCheckable(true);
			a->setChecked(fw->isVisible());
#if QT_VERSION >= 0x050000
			QObject::connect(a, &QAction::toggled, fw, &FloatingWidget::setVisible);
#else
			QObject::connect(a, SIGNAL(toggled(bool)), fw, SLOT(setVisible(bool)));
#endif
		}
	}

	return m;
}

QByteArray ContainerWidget::saveState() const
{
	QByteArray ba;
	QDataStream out(&ba, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_5);
	out << (quint32) 0x00001337; // Magic
	out << (quint32) 1; // Version

	// Save state of floating contents
	out << _floatings.count();
	for (int i = 0; i < _floatings.count(); ++i)
	{
		FloatingWidget* fw = _floatings.at(i);
		out << fw->content()->uniqueName();
		out << fw->saveGeometry();
		out << fw->isVisible();
	}

	// Walk through layout for splitters
	// Well.. there actually shouldn't be more than one
	for (int i = 0; i < _mainLayout->count(); ++i)
	{
		QLayoutItem* li = _mainLayout->itemAt(i);
		if (!li->widget())
			continue;
		saveGeometryWalk(out, li->widget());
	}

	return ba;
}

bool ContainerWidget::restoreState(const QByteArray& data)
{
	QDataStream in(data);
	in.setVersion(QDataStream::Qt_4_5);

	quint32 magic = 0;
	in >> magic;
	if (magic != 0x00001337)
		return false;

	quint32 version = 0;
	in >> version;
	if (version != 1)
		return false;

	QList<FloatingWidget*> oldFloatings = _floatings;
	QList<SectionWidget*> oldSections = _sections;

	// Restore floating widgets
	QList<FloatingWidget*> floatings;
	bool success = restoreFloatingWidgets(in, floatings);
	if (!success)
	{
		qWarning() << "Could not restore floatings completely";
	}

	// Restore splitters and section widgets
	QList<SectionWidget*> sections;
	success = restoreSectionWidgets(in, NULL, sections);
	if (!success)
	{
		qWarning() << "Could not restore sections completely";
	}

	_floatings = floatings;
	_sections = sections;

	// TODO Handle contents which are not mentioned by deserialized data
	// ...

	// Delete old objects
	QLayoutItem* old = _mainLayout->takeAt(0);
	_mainLayout->addWidget(_splitter);
	delete old;
	qDeleteAll(oldFloatings);
	qDeleteAll(oldSections);

	return success;
}

QRect ContainerWidget::outerTopDropRect() const
{
	QRect r = rect();
	int h = r.height() / 100 * 5;
	return QRect(r.left(), r.top(), r.width(), h);
}

QRect ContainerWidget::outerRightDropRect() const
{
	QRect r = rect();
	int w = r.width() / 100 * 5;
	return QRect(r.right() - w, r.top(), w, r.height());
}

QRect ContainerWidget::outerBottomDropRect() const
{
	QRect r = rect();
	int h = r.height() / 100 * 5;
	return QRect(r.left(), r.bottom() - h, r.width(), h);
}

QRect ContainerWidget::outerLeftDropRect() const
{
	QRect r = rect();
	int w = r.width() / 100 * 5;
	return QRect(r.left(), r.top(), w, r.height());
}

///////////////////////////////////////////////////////////////////////
// PRIVATE API BEGINS HERE
///////////////////////////////////////////////////////////////////////

SectionWidget* ContainerWidget::newSectionWidget()
{
	SectionWidget* sw = new SectionWidget(this);
	_sections.append(sw);
	return sw;
}

SectionWidget* ContainerWidget::dropContent(const InternalContentData& data, SectionWidget* targetSection, DropArea area, bool autoActive)
{
	SectionWidget* ret = NULL;

	// Drop on outer area
	if (!targetSection)
	{
		switch (area)
		{
		case TopDropArea:
			ret = dropContentOuterHelper(_mainLayout, data, Qt::Vertical, false);
			break;
		case RightDropArea:
			ret = dropContentOuterHelper(_mainLayout, data, Qt::Horizontal, true);
			break;
		case BottomDropArea:
			ret = dropContentOuterHelper(_mainLayout, data, Qt::Vertical, true);
			break;
		case LeftDropArea:
			ret = dropContentOuterHelper(_mainLayout, data, Qt::Horizontal, false);
			break;
		default:
			return NULL;
		}
		return NULL;
	}

	QSplitter* targetSectionSplitter = findParentSplitter(targetSection);

	// Drop logic based on area.
	switch (area)
	{
	case TopDropArea:
	{
		SectionWidget* sw = newSectionWidget();
		sw->addContent(data, true);
		if (targetSectionSplitter->orientation() == Qt::Vertical)
		{
			const int index = targetSectionSplitter->indexOf(targetSection);
			targetSectionSplitter->insertWidget(index, sw);
		}
		else
		{
			const int index = targetSectionSplitter->indexOf(targetSection);
			QSplitter* s = newSplitter(Qt::Vertical);
			s->addWidget(sw);
			s->addWidget(targetSection);
			targetSectionSplitter->insertWidget(index, s);
		}
		ret = sw;
		break;
	}
	case RightDropArea:
	{
		SectionWidget* sw = newSectionWidget();
		sw->addContent(data, true);
		if (targetSectionSplitter->orientation() == Qt::Horizontal)
		{
			const int index = targetSectionSplitter->indexOf(targetSection);
			targetSectionSplitter->insertWidget(index + 1, sw);
		}
		else
		{
			const int index = targetSectionSplitter->indexOf(targetSection);
			QSplitter* s = newSplitter(Qt::Horizontal);
			s->addWidget(targetSection);
			s->addWidget(sw);
			targetSectionSplitter->insertWidget(index, s);
		}
		ret = sw;
		break;
	}
	case BottomDropArea:
	{
		SectionWidget* sw = newSectionWidget();
		sw->addContent(data, true);
		if (targetSectionSplitter->orientation() == Qt::Vertical)
		{
			int index = targetSectionSplitter->indexOf(targetSection);
			targetSectionSplitter->insertWidget(index + 1, sw);
		}
		else
		{
			int index = targetSectionSplitter->indexOf(targetSection);
			QSplitter* s = newSplitter(Qt::Vertical);
			s->addWidget(targetSection);
			s->addWidget(sw);
			targetSectionSplitter->insertWidget(index, s);
		}
		ret = sw;
		break;
	}
	case LeftDropArea:
	{
		SectionWidget* sw = newSectionWidget();
		sw->addContent(data, true);
		if (targetSectionSplitter->orientation() == Qt::Horizontal)
		{
			int index = targetSectionSplitter->indexOf(targetSection);
			targetSectionSplitter->insertWidget(index, sw);
		}
		else
		{
			QSplitter* s = newSplitter(Qt::Horizontal);
			s->addWidget(sw);
			int index = targetSectionSplitter->indexOf(targetSection);
			targetSectionSplitter->insertWidget(index, s);
			s->addWidget(targetSection);
		}
		ret = sw;
		break;
	}
	case CenterDropArea:
	{
		targetSection->addContent(data, autoActive);
		ret = targetSection;
		break;
	}
	default:
		break;
	}
	return ret;
}

void ContainerWidget::addSection(SectionWidget* section)
{
	// Create default splitter.
	if (!_splitter)
	{
		_splitter = newSplitter(_orientation);
		_mainLayout->addWidget(_splitter, 0, 0);
	}
	if (_splitter->indexOf(section) != -1)
	{
		qWarning() << Q_FUNC_INFO << QString("Section has already been added");
		return;
	}
	_splitter->addWidget(section);
}

SectionWidget* ContainerWidget::sectionAt(const QPoint& pos) const
{
	const QPoint gpos = mapToGlobal(pos);
	for (int i = 0; i < _sections.size(); ++i)
	{
		SectionWidget* sw = _sections[i];
		if (sw->rect().contains(sw->mapFromGlobal(gpos)))
		{
			return sw;
		}
	}
	return 0;
}

SectionWidget* ContainerWidget::dropContentOuterHelper(QLayout* l, const InternalContentData& data, Qt::Orientation orientation, bool append)
{
	SectionWidget* sw = newSectionWidget();
	sw->addContent(data, true);

	QSplitter* oldsp = findImmediateSplitter(this);
	if (oldsp->orientation() == orientation
			|| oldsp->count() == 1)
	{
		oldsp->setOrientation(orientation);
		if (append)
			oldsp->addWidget(sw);
		else
			oldsp->insertWidget(0, sw);
	}
	else
	{
		QSplitter* sp = newSplitter(orientation);
		if (append)
		{
#if QT_VERSION >= 0x050000
			QLayoutItem* li = l->replaceWidget(oldsp, sp);
			sp->addWidget(oldsp);
			sp->addWidget(sw);
			delete li;
#else
			int index = l->indexOf(oldsp);
			QLayoutItem* li = l->takeAt(index);
			l->addWidget(sp);
			sp->addWidget(oldsp);
			sp->addWidget(sw);
			delete li;
#endif
		}
		else
		{
#if QT_VERSION >= 0x050000
			sp->addWidget(sw);
			QLayoutItem* li = l->replaceWidget(oldsp, sp);
			sp->addWidget(oldsp);
			delete li;
#else
			sp->addWidget(sw);
			int index = l->indexOf(oldsp);
			QLayoutItem* li = l->takeAt(index);
			l->addWidget(sp);
			sp->addWidget(oldsp);
			delete li;
#endif
		}
	}
	return sw;
}

void ContainerWidget::saveGeometryWalk(QDataStream& out, QWidget* widget) const
{
	QSplitter* sp = NULL;
	SectionWidget* sw = NULL;

	if (!widget)
	{
		out << 0;
	}
	else if ((sp = dynamic_cast<QSplitter*>(widget)) != NULL)
	{
		out << 1; // Type = QSplitter
		out << ((sp->orientation() == Qt::Horizontal) ? (int) 1 : (int) 2);
		out << sp->count();
		out << sp->sizes();
		for (int i = 0; i < sp->count(); ++i)
		{
			saveGeometryWalk(out, sp->widget(i));
		}
	}
	else if ((sw = dynamic_cast<SectionWidget*>(widget)) != NULL)
	{
		out << 2; // Type = SectionWidget
		out << sw->currentIndex();
		out << sw->contents().count();
		const QList<SectionContent::RefPtr>& contents = sw->contents();
		for (int i = 0; i < contents.count(); ++i)
		{
			out << contents[i]->uniqueName();
		}
	}
}

bool ContainerWidget::restoreFloatingWidgets(QDataStream& in, QList<FloatingWidget*>& floatings)
{
	int fwCount = 0;
	in >> fwCount;
	if (fwCount <= 0)
		return true;

	for (int i = 0; i < fwCount; ++i)
	{
		QString uname;
		in >> uname;
		QByteArray geom;
		in >> geom;
		bool visible = false;
		in >> visible;
		qDebug() << "Restore FloatingWidget" << uname << geom << visible;

		const SectionContent::RefPtr sc = SectionContent::LookupMapByName.value(uname).toStrongRef();
		if (!sc)
		{
			qWarning() << "Can not find SectionContent:" << uname;
			continue;
		}

		InternalContentData data;
		if (!this->takeContent(sc, data))
			continue;

		FloatingWidget* fw = new FloatingWidget(this, sc, data.titleWidget, data.contentWidget, this);
		fw->restoreGeometry(geom);
		fw->setVisible(visible);
		floatings.append(fw);
		data.titleWidget->_fw = fw; // $mfreiholz: Don't look at it :-< It's more than ugly...
	}
	return true;
}

bool ContainerWidget::restoreSectionWidgets(QDataStream& in, QSplitter* currentSplitter, QList<SectionWidget*>& sections)
{
	int type;
	in >> type;

	// Splitter
	if (type == 1)
	{
		int orientation, count;
		QList<int> sizes;
		in >> orientation >> count >> sizes;

		QSplitter* sp = newSplitter((Qt::Orientation) orientation);
		for (int i = 0; i < count; ++i)
		{
			if (!restoreSectionWidgets(in, sp, sections))
				return false;
		}
		sp->setSizes(sizes);

		if (!currentSplitter)
			_splitter = sp;
		else
			currentSplitter->addWidget(sp);
	}
	// Section
	else if (type == 2)
	{
		if (!currentSplitter)
		{
			qWarning() << "Missing splitter object for section";
			return false;
		}

		int currentIndex, count;
		in >> currentIndex >> count;

		SectionWidget* sw = new SectionWidget(this);
		for (int i = 0; i < count; ++i)
		{
			QString uname;
			in >> uname;
			const SectionContent::RefPtr sc = SectionContent::LookupMapByName.value(uname).toStrongRef();
			if (!sc)
			{
				qWarning() << "Can not find SectionContent:" << uname;
				continue;
			}

			InternalContentData data;
			if (!this->takeContent(sc, data))
				continue;

			sw->addContent(sc);
		}
		sw->setCurrentIndex(currentIndex);
		currentSplitter->addWidget(sw);
		sections.append(sw);
	}
	// Unknown
	else
	{
		qDebug() << QString();
	}

	return true;
}

bool ContainerWidget::takeContent(const SectionContent::RefPtr& sc, InternalContentData& data)
{
	// Search in sections
	bool found = false;
	for (int i = 0; i < _sections.count() && !found; ++i)
	{
		found = _sections.at(i)->takeContent(sc->uid(), data);
	}

	// Search in floating widgets
	for (int i = 0; i < _floatings.count() && !found; ++i)
	{
		found = _floatings.at(i)->content()->uid() == sc->uid();
		_floatings.at(i)->takeContent(data);
	}

	return found;
}

void ContainerWidget::onActionToggleSectionContentVisibility(bool visible)
{
	QAction* a = qobject_cast<QAction*>(sender());
	if (!a)
		return;
	const int uid = a->property("uid").toInt();
	qDebug() << "Change visibility of" << uid << visible;
}

ADS_NAMESPACE_END
