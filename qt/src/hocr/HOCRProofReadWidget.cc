/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * HOCRProofReadWidget.cc
 * Copyright (C) 2020 Sandro Mani <manisandro@gmail.com>
 *
 * gImageReader is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gImageReader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QTreeView>
#include <QVBoxLayout>

#include "Displayer.hh"
#include "HOCRDocument.hh"
#include "HOCRProofReadWidget.hh"
#include "HOCRSpellChecker.hh"
#include "MainWindow.hh"
#include "SourceManager.hh"


class HOCRProofReadWidget::LineEdit : public QLineEdit {
public:
	LineEdit(HOCRProofReadWidget* proofReadWidget, HOCRItem* wordItem, QWidget* parent = nullptr) :
		QLineEdit(wordItem->text(), parent), m_proofReadWidget(proofReadWidget), m_wordItem(wordItem) {
		connect(this, &LineEdit::textChanged, this, &LineEdit::onTextChanged);

		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());
		connect(document, &HOCRDocument::dataChanged, this, &LineEdit::onModelDataChanged);
		connect(document, &HOCRDocument::itemAttributeChanged, this, &LineEdit::onAttributeChanged);

		QModelIndex index = document->indexAtItem(m_wordItem);
		setStyleSheet( getStyle( document->indexIsMisspelledWord(index) ) );

		QFont ft = font();
		ft.setBold(m_wordItem->fontBold());
		ft.setItalic(m_wordItem->fontItalic());
		setFont(ft);
	}
	const HOCRItem* item() const { return m_wordItem; }

private:
	HOCRProofReadWidget* m_proofReadWidget = nullptr;
	HOCRItem* m_wordItem = nullptr;
	bool m_blockSetText = false;

	QString getStyle(bool misspelled) const {
		QStringList styles;
		if(misspelled) {
			styles.append( "color: red;" );
		}
		QString confStyle = m_proofReadWidget->confidenceStyle(m_wordItem->getTitleAttributes()["x_wconf"].toInt());
		if(!confStyle.isEmpty()) {
			styles.append(confStyle);
		}
		return styles.isEmpty() ? "" : QString("QLineEdit {%1}").arg(styles.join(" "));
	}

	void onTextChanged() {
		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());

		// Update data in document
		QModelIndex index = document->indexAtItem(m_wordItem);
		m_blockSetText = true;
		document->setData(index, text(), Qt::EditRole);
		m_blockSetText = false;
	}
	void onModelDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());
		QItemSelectionRange range(topLeft, bottomRight);
		QModelIndex index = document->indexAtItem(m_wordItem);
		if(range.contains(index)) {
			if(roles.contains(Qt::DisplayRole) && !m_blockSetText) {
				setText(m_wordItem->text());
			}
			if(roles.contains(Qt::ForegroundRole)) {
				setStyleSheet( getStyle( document->indexIsMisspelledWord(index) ) );
			}
		}
	}
	void onAttributeChanged(const QModelIndex& index, const QString& name, const QString& /*value*/) {
		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());
		if(document->itemAtIndex(index) == m_wordItem) {
			if(name == "bold" || name == "italic") {
				QFont ft = font();
				ft.setBold(m_wordItem->fontBold());
				ft.setItalic(m_wordItem->fontItalic());
				setFont(ft);
			} else if(name == "title:bbox") {
				QPoint sceneCorner = MAIN->getDisplayer()->getSceneBoundingRect().toRect().topLeft();
				QRect sceneBBox = m_wordItem->bbox().translated(sceneCorner);
				QPoint bottomLeft = MAIN->getDisplayer()->mapFromScene(sceneBBox.bottomLeft());
				QPoint bottomRight = MAIN->getDisplayer()->mapFromScene(sceneBBox.bottomRight());
				int frameX = parentWidget()->parentWidget()->parentWidget()->pos().x();
				move(bottomLeft.x() - frameX, 0);
				setFixedWidth(bottomRight.x() - bottomLeft.x() + 6); // 6: border + padding
			}
		}
	}
	void keyPressEvent(QKeyEvent* ev) override {
		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());

		bool nextLine = (ev->modifiers() == Qt::NoModifier && ev->key() == Qt::Key_Down) || (ev->key() == Qt::Key_Tab && m_wordItem == m_wordItem->parent()->children().last());
		bool prevLine = (ev->modifiers() == Qt::NoModifier && ev->key() == Qt::Key_Up) || (ev->key() == Qt::Key_Backtab && m_wordItem == m_wordItem->parent()->children().first());
		if(nextLine || prevLine) {
			bool next = false;
			QModelIndex index;
			if(nextLine) {
				next = true;
				index = document->indexAtItem(m_wordItem);
				// Move to first word of next line
				index = document->prevOrNextIndex(next, index, "ocr_line");
				index = document->prevOrNextIndex(true, index, "ocrx_word");
			} else if(prevLine) {
				index = document->indexAtItem(m_wordItem);
				// Move to last word of prev line
				index = document->prevOrNextIndex(false, index, "ocr_line");
				index = document->prevOrNextIndex(false, index, "ocrx_word");
			}
			m_proofReadWidget->documentTree()->setCurrentIndex(index);
		} else if(ev->key() == Qt::Key_Space && ev->modifiers() == Qt::ControlModifier) {
			// Spelling menu
			QModelIndex index = document->indexAtItem(m_wordItem);
			QMenu menu;
			document->addSpellingActions(&menu, index);
			menu.exec(mapToGlobal(QPoint(0, -menu.sizeHint().height())));
		} else if(ev->key() == Qt::Key_B && ev->modifiers() == Qt::ControlModifier) {
			// Bold
			QModelIndex index = document->indexAtItem(m_wordItem);
			document->editItemAttribute(index, "bold", m_wordItem->fontBold() ? "0" : "1");
		} else if(ev->key() == Qt::Key_I && ev->modifiers() == Qt::ControlModifier) {
			// Italic
			QModelIndex index = document->indexAtItem(m_wordItem);
			document->editItemAttribute(index, "italic", m_wordItem->fontItalic() ? "0" : "1");
		} else if((ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down) && ev->modifiers() & Qt::ControlModifier) {
			// Adjust bbox top/bottom
			QModelIndex index = document->indexAtItem(m_wordItem);
			QRect bbox = m_wordItem->bbox();
			if(ev->modifiers() & Qt::ShiftModifier) {
				bbox.setBottom(bbox.bottom() + (ev->key() == Qt::Key_Up ? -1 : 1));
			} else {
				bbox.setTop(bbox.top() + (ev->key() == Qt::Key_Up ? -1 : 1));
			}
			QString bboxstr = QString("%1 %2 %3 %4").arg(bbox.left()).arg(bbox.top()).arg(bbox.right()).arg(bbox.bottom());
			document->editItemAttribute(index, "title:bbox", bboxstr);
		} else if((ev->key() == Qt::Key_Left || ev->key() == Qt::Key_Right) && ev->modifiers() & Qt::ControlModifier) {
			// Adjust bbox left/right
			QModelIndex index = document->indexAtItem(m_wordItem);
			QRect bbox = m_wordItem->bbox();
			if(ev->modifiers() & Qt::ShiftModifier) {
				bbox.setRight(bbox.right() + (ev->key() == Qt::Key_Left ? -1 : 1));
			} else {
				bbox.setLeft(bbox.left() + (ev->key() == Qt::Key_Left ? -1 : 1));
			}
			QString bboxstr = QString("%1 %2 %3 %4").arg(bbox.left()).arg(bbox.top()).arg(bbox.right()).arg(bbox.bottom());
			document->editItemAttribute(index, "title:bbox", bboxstr);
		} else if(ev->key() == Qt::Key_D && ev->modifiers() == Qt::ControlModifier) {
			// Divide
			QModelIndex index = document->indexAtItem(m_wordItem);
			document->splitItemText(index, cursorPosition());
		} else if(ev->key() == Qt::Key_M && ev->modifiers() & Qt::ControlModifier) {
			// Merge
			QModelIndex index = document->indexAtItem(m_wordItem);
			document->mergeItemText(index, (ev->modifiers() & Qt::ShiftModifier) != 0);
		} else {
			QLineEdit::keyPressEvent(ev);
		}
	}
	void focusInEvent(QFocusEvent* ev) override {
		HOCRDocument* document = static_cast<HOCRDocument*>(m_proofReadWidget->documentTree()->model());
		m_proofReadWidget->documentTree()->setCurrentIndex(document->indexAtItem(m_wordItem));
		m_proofReadWidget->setConfidenceLabel(m_wordItem->getTitleAttributes()["x_wconf"].toInt());
		QLineEdit::focusInEvent(ev);
	}
};


HOCRProofReadWidget::HOCRProofReadWidget(QTreeView* treeView, QWidget* parent)
	: QFrame(parent), m_treeView(treeView) {
	QVBoxLayout* layout = new QVBoxLayout;
	layout->setContentsMargins(2, 2, 2, 2);
	layout->setSpacing(2);
	setLayout(layout);

	QWidget* linesWidget = new QWidget();
	m_linesLayout = new QVBoxLayout();
	m_linesLayout->setContentsMargins(0, 0, 0, 0);
	m_linesLayout->setSpacing(2);
	linesWidget->setLayout(m_linesLayout);
	layout->addWidget(linesWidget);

	m_controlsWidget = new QWidget();
	m_controlsWidget->setLayout(new QHBoxLayout);
	m_controlsWidget->layout()->setSpacing(2);
	m_controlsWidget->layout()->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_controlsWidget);

	QFont smallFont;
	smallFont.setPointSizeF(0.8 * smallFont.pointSizeF());

	m_confidenceLabel = new QLabel();
	m_confidenceLabel->setFont(smallFont);
	m_controlsWidget->layout()->addWidget(m_confidenceLabel);

	m_controlsWidget->layout()->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));

	QLabel* helpButton = new QLabel(QString("<a href=\"#help\">%1</a>").arg(_("Keyboard shortcuts")));
	helpButton->setFont(smallFont);
	connect(helpButton, &QLabel::linkActivated, this, &HOCRProofReadWidget::showShortcutsDialog);
	m_controlsWidget->layout()->addWidget(helpButton);

	setObjectName("proofReadWidget");
	setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
	setAutoFillBackground(true);

	setStyleSheet("QLineEdit { border: 1px solid #ddd; }");

	connect(m_treeView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &HOCRProofReadWidget::setCurrentRow);

	HOCRDocument* document = static_cast<HOCRDocument*>(m_treeView->model());

	// Clear for rebuild if structure changes
	connect(document, &HOCRDocument::rowsAboutToBeRemoved, this, &HOCRProofReadWidget::clear);
	connect(document, &HOCRDocument::rowsAboutToBeInserted, this, &HOCRProofReadWidget::clear);
	connect(document, &HOCRDocument::rowsAboutToBeMoved, this, &HOCRProofReadWidget::clear);
	connect(document, &HOCRDocument::rowsRemoved, this, &HOCRProofReadWidget::updateRows);
	connect(document, &HOCRDocument::rowsInserted, this, &HOCRProofReadWidget::updateRows);
	connect(document, &HOCRDocument::rowsMoved, this, &HOCRProofReadWidget::updateRows);
	connect(MAIN->getDisplayer(), &Displayer::viewportChanged, this, &HOCRProofReadWidget::repositionWidget);
	connect(MAIN->getSourceManager(), &SourceManager::sourceChanged, this, &HOCRProofReadWidget::hide);
}

void HOCRProofReadWidget::clear() {
	qDeleteAll(m_currentLines);
	m_currentLines.clear();
	m_currentLine = nullptr;
	m_confidenceLabel->setText("");
	m_confidenceLabel->setStyleSheet("");
	hide();
}

void HOCRProofReadWidget::updateRows() {
	setCurrentRow(m_treeView->currentIndex());
}

void HOCRProofReadWidget::setCurrentRow(const QModelIndex& current) {
	const int nrSurroundingLines = 1;

	HOCRDocument* document = static_cast<HOCRDocument*>(m_treeView->model());
	const HOCRItem* item = document->itemAtIndex(current);
	if(!item) {
		clear();
		return;
	}
	const HOCRItem* lineItem = nullptr;
	const HOCRItem* wordItem = nullptr;
	if(item->itemClass() == "ocrx_word") {
		lineItem = item->parent();
		wordItem = item;
	} else if(item->itemClass() == "ocr_line") {
		lineItem = item;
	} else {
		clear();
		return;
	}

	const QVector<HOCRItem*>& siblings = lineItem->parent()->children();
	int targetLine = lineItem->index();
	if(lineItem != m_currentLine) {
		// Rebuild widget
		QMap<const HOCRItem*, QWidget*> newLines;
		int insPos = 0;
		for(int i = qMax(0, targetLine - nrSurroundingLines), j = qMin(siblings.size() - 1, targetLine + nrSurroundingLines); i <= j; ++i) {
			HOCRItem* linei = siblings[i];
			if(m_currentLines.contains(linei)) {
				newLines[linei] = m_currentLines.take(linei);
				insPos = m_linesLayout->indexOf(newLines[linei]) + 1;
			} else {
				QWidget* lineWidget = new QWidget();
				for(HOCRItem* word : siblings[i]->children()) {
					new LineEdit(this, word, lineWidget); // Add as child to lineWidget
				}
				m_linesLayout->insertWidget(insPos++, lineWidget);
				newLines.insert(linei, lineWidget);
			}
		}
		qDeleteAll(m_currentLines);
		m_currentLines = newLines;
		m_currentLine = lineItem;
		repositionWidget();
	}

	// Select selected word or first item of middle line
	LineEdit* focusLineEdit = static_cast<LineEdit*>(m_currentLines[lineItem]->children()[wordItem ? wordItem->index() : 0]);
	if(focusLineEdit && !m_treeView->hasFocus()) {
		focusLineEdit->setFocus();
		focusLineEdit->selectAll();
	}

}

void HOCRProofReadWidget::repositionWidget() {

	if(m_currentLines.isEmpty()) {
		return;
	}

	// Position frame
	Displayer* displayer = MAIN->getDisplayer();
	int frameXmin = std::numeric_limits<int>::max();
	int frameXmax = 0;
	int frameY = std::numeric_limits<int>::min();
	QPoint sceneCorner = displayer->getSceneBoundingRect().toRect().topLeft();
	for(QWidget* lineWidget : m_currentLines) {
		if(lineWidget->children().isEmpty()) {
			continue;
		}
		// First word
		LineEdit* lineEdit = static_cast<LineEdit*>(lineWidget->children()[0]);
		QPoint bottomLeft = displayer->mapFromScene(lineEdit->item()->bbox().translated(sceneCorner).bottomLeft());
		frameXmin = std::min(frameXmin, bottomLeft.x());
		frameY = std::max(frameY, bottomLeft.y());
	}

	// Recompute font sizes so that text matches original as closely as possible
	QFont ft = font();
	QFontMetrics fm(ft);
	double minFactor = std::numeric_limits<double>::max();
	// First pass: min scaling factor, move to correct location
	for(QWidget* lineWidget : m_currentLines) {
		for(int i = 0, n = lineWidget->children().count(); i < n; ++i) {
			LineEdit* lineEdit = static_cast<LineEdit*>(lineWidget->children()[i]);
			QRect sceneBBox = lineEdit->item()->bbox().translated(sceneCorner);
			QPoint bottomLeft = displayer->mapFromScene(sceneBBox.bottomLeft());
			QPoint bottomRight = displayer->mapFromScene(sceneBBox.bottomRight());
			double factor = (bottomRight.x() - bottomLeft.x()) / double(fm.horizontalAdvance(lineEdit->text()));
			minFactor = std::min(factor, minFactor);
			lineEdit->move(bottomLeft.x() - frameXmin, 0);
			lineEdit->setFixedWidth(bottomRight.x() - bottomLeft.x() + 6); // 6: border + padding
			frameXmax = std::max(frameXmax, bottomRight.x() + 6);
		}
	}
	// Second pass: apply font sizes, set line heights
	ft.setPointSizeF(ft.pointSizeF() * minFactor);
	fm = QFontMetrics(ft);
	for(QWidget* lineWidget : m_currentLines) {
		for(int i = 0, n = lineWidget->children().count(); i < n; ++i) {
			LineEdit* lineEdit = static_cast<LineEdit*>(lineWidget->children()[i]);
			QFont lineEditFont = lineEdit->font();
			lineEditFont.setPointSizeF(ft.pointSizeF());
			lineEdit->setFont(lineEditFont);
			lineEdit->setFixedHeight(fm.height() + 5);
		}
		lineWidget->setFixedHeight(fm.height() + 10);
	}

	updateGeometry();
	move(frameXmin - layout()->spacing(), frameY + 10);
	resize(frameXmax - frameXmin + 2 + 2 * layout()->spacing(), m_currentLines.size() * (fm.height() + 10) + 2 * layout()->spacing() + m_controlsWidget->sizeHint().height());
	show();
}

void HOCRProofReadWidget::showShortcutsDialog() {
	QString text = QString(_(
	                           "<table>"
	                           "<tr><td>Tab</td><td>Next field</td></tr>"
	                           "<tr><td>Shift+Tab</td><td>Previous field</td></tr>"
	                           "<tr><td>Down</td><td>Next line</td></tr>"
	                           "<tr><td>Up</td><td>Previous line</td></tr>"
	                           "<tr><td>Ctrl+Space</td><td>Spelling suggestions</td></tr>"
	                           "<tr><td>Ctrl+B</td><td>Toggle bold</td></tr>"
	                           "<tr><td>Ctrl+I</td><td>Toggle italic</td></tr>"
	                           "<tr><td>Ctrl+D</td><td>Divide word at cursor position</td></tr>"
	                           "<tr><td>Ctrl+M</td><td>Merge with previous word</td></tr>"
	                           "<tr><td>Ctrl+Shift+M</td><td>Merge with next word</td></tr>"
	                           "<tr><td>Ctrl+{Left,Right}</td><td>Adjust left bounding box edge</td></tr>"
	                           "<tr><td>Ctrl+Shift+{Left,Right}</td><td>Adjust right bounding box edge</td></tr>"
	                           "<tr><td>Ctrl+{Up,Down}</td><td>Adjust top bounding box edge</td></tr>"
	                           "<tr><td>Ctrl+Shift+{Up,Down}</td><td>Adjust bottom bounding box edge</td></tr>"
	                           "</table>"
	                       ));
	QMessageBox(QMessageBox::NoIcon, _("Keyboard Shortcuts"), text, QMessageBox::Close, MAIN).exec();
}

QString HOCRProofReadWidget::confidenceStyle(int wconf) const {
	if(wconf < 70) {
		return "background: #ffb2b2;";
	} else if(wconf < 80) {
		return "background: #ffdab0;";
	} else if(wconf < 90) {
		return "background: #fffdb4;";
	}
	return QString();
}

void HOCRProofReadWidget::setConfidenceLabel(int wconf) {
	m_confidenceLabel->setText(_("Confidence: %1").arg(wconf));
	QString style = confidenceStyle(wconf);
	m_confidenceLabel->setStyleSheet(!style.isEmpty() ? QString("QLabel { %1 }").arg(style) : "");
}