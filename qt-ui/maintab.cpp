/*
 * maintab.cpp
 *
 * classes for the "notebook" area of the main window of Subsurface
 *
 */
#include "maintab.h"
#include "mainwindow.h"
#include "../helpers.h"
#include "../statistics.h"
#include "divelistview.h"
#include "modeldelegates.h"
#include "globe.h"
#include "completionmodels.h"
#include "diveplanner.h"
#include "qthelper.h"

#include <QLabel>
#include <QCompleter>
#include <QDebug>
#include <QSet>
#include <QTableView>
#include <QSettings>
#include <QPalette>

MainTab::MainTab(QWidget *parent) : QTabWidget(parent),
				    weightModel(new WeightModel()),
				    cylindersModel(new CylindersModel()),
				    editMode(NONE)
{
	ui.setupUi(this);
	ui.cylinders->setModel(cylindersModel);
	ui.weights->setModel(weightModel);
	ui.diveNotesMessage->hide();
	ui.diveEquipmentMessage->hide();
	ui.notesButtonBox->hide();
	ui.equipmentButtonBox->hide();
	ui.diveNotesMessage->setCloseButtonVisible(false);
	ui.diveEquipmentMessage->setCloseButtonVisible(false);

	if (qApp->style()->objectName() == "oxygen")
		setDocumentMode(true);
	else
		setDocumentMode(false);

	// we start out with the fields read-only; once things are
	// filled from a dive, they are made writeable
	setEnabled(false);

	ui.location->installEventFilter(this);
	ui.coordinates->installEventFilter(this);
	ui.divemaster->installEventFilter(this);
	ui.buddy->installEventFilter(this);
	ui.suit->installEventFilter(this);
	ui.notes->viewport()->installEventFilter(this);
	ui.rating->installEventFilter(this);
	ui.visibility->installEventFilter(this);
	ui.airtemp->installEventFilter(this);
	ui.watertemp->installEventFilter(this);
	ui.dateTimeEdit->installEventFilter(this);
	ui.tagWidget->installEventFilter(this);

	QList<QObject *> statisticsTabWidgets = ui.statisticsTab->children();
	Q_FOREACH(QObject* obj, statisticsTabWidgets) {
		QLabel* label = qobject_cast<QLabel *>(obj);
		if (label)
			label->setAlignment(Qt::AlignHCenter);
	}
	ui.cylinders->setTitle(tr("Cylinders"));
	ui.cylinders->setBtnToolTip(tr("Add Cylinder"));
	connect(ui.cylinders, SIGNAL(addButtonClicked()), this, SLOT(addCylinder_clicked()));

	ui.weights->setTitle(tr("Weights"));
	ui.weights->setBtnToolTip(tr("Add Weight System"));
	connect(ui.weights, SIGNAL(addButtonClicked()), this, SLOT(addWeight_clicked()));

	connect(ui.cylinders->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editCylinderWidget(QModelIndex)));
	connect(ui.weights->view(), SIGNAL(clicked(QModelIndex)), this, SLOT(editWeightWidget(QModelIndex)));
	connect(ui.notesButtonBox, SIGNAL(accepted()), this, SLOT(acceptChanges()));
	connect(ui.notesButtonBox, SIGNAL(rejected()), this, SLOT(rejectChanges()));
	connect(ui.equipmentButtonBox, SIGNAL(accepted()), this, SLOT(acceptChanges()));
	connect(ui.equipmentButtonBox, SIGNAL(rejected()), this, SLOT(rejectChanges()));

	ui.cylinders->view()->setItemDelegateForColumn(CylindersModel::TYPE, new TankInfoDelegate());
	ui.weights->view()->setItemDelegateForColumn(WeightModel::TYPE, new WSInfoDelegate());

	completers.buddy = new QCompleter(BuddyCompletionModel::instance(), ui.buddy);
	completers.divemaster = new QCompleter(DiveMasterCompletionModel::instance(), ui.divemaster);
	completers.location = new QCompleter(LocationCompletionModel::instance(), ui.location);
	completers.suit = new QCompleter(SuitCompletionModel::instance(), ui.suit);
	completers.tags = new QCompleter(TagCompletionModel::instance(), ui.tagWidget);
	ui.buddy->setCompleter(completers.buddy);
	ui.divemaster->setCompleter(completers.divemaster);
	ui.location->setCompleter(completers.location);
	ui.suit->setCompleter(completers.suit);
	ui.tagWidget->setCompleter(completers.tags);
	setMinimumHeight(0);
	setMinimumWidth(0);

	// Current display of things on Gnome3 looks like shit, so
	// let`s fix that.
	if (isGnome3Session()) {
		QPalette p;
		p.setColor(QPalette::Window, QColor(Qt::white));
		ui.scrollArea->viewport()->setPalette(p);
		ui.scrollArea_2->viewport()->setPalette(p);
		ui.scrollArea_3->viewport()->setPalette(p);
		ui.scrollArea_4->viewport()->setPalette(p);
	}
}

void MainTab::addDiveStarted()
{
	enableEdition();
	editMode = ADD;
}

void MainTab::enableEdition()
{
	if (selected_dive < 0 || editMode != NONE)
		return;

	mainWindow()->dive_list()->setEnabled(false);
	// We may be editing one or more dives here. backup everything.
	notesBackup.clear();
	ui.notesButtonBox->show();
	ui.equipmentButtonBox->show();

	if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		// we are editing trip location and notes
		ui.diveNotesMessage->setText(tr("This trip is being edited. Select Save or Undo when ready."));
		ui.diveNotesMessage->animatedShow();
		ui.diveEquipmentMessage->setText(tr("This trip is being edited. Select Save or Undo when ready."));
		ui.diveEquipmentMessage->animatedShow();
		notesBackup[NULL].notes = ui.notes->toPlainText();
		notesBackup[NULL].location = ui.location->text();
		editMode = TRIP;
	} else {
		ui.diveNotesMessage->setText(tr("This dive is being edited. Select Save or Undo when ready."));
		ui.diveNotesMessage->animatedShow();
		ui.diveEquipmentMessage->setText(tr("This dive is being edited. Select Save or Undo when ready."));
		ui.diveEquipmentMessage->animatedShow();

		// We may be editing one or more dives here. backup everything.
		struct dive *mydive;
		for (int i = 0; i < dive_table.nr; i++) {
			mydive = get_dive(i);
			if (!mydive)
				continue;
			if (!mydive->selected)
				continue;

			notesBackup[mydive].buddy = QString(mydive->buddy);
			notesBackup[mydive].suit = QString(mydive->suit);
			notesBackup[mydive].notes = QString(mydive->notes);
			notesBackup[mydive].divemaster = QString(mydive->divemaster);
			notesBackup[mydive].location = QString(mydive->location);
			notesBackup[mydive].rating = mydive->rating;
			notesBackup[mydive].visibility = mydive->visibility;
			notesBackup[mydive].latitude = mydive->latitude;
			notesBackup[mydive].longitude = mydive->longitude;
			notesBackup[mydive].coordinates  = ui.coordinates->text();
			notesBackup[mydive].airtemp = get_temperature_string(mydive->airtemp, true);
			notesBackup[mydive].watertemp = get_temperature_string(mydive->watertemp, true);
			notesBackup[mydive].datetime = QDateTime::fromTime_t(mydive->when - gettimezoneoffset()).toString(QString("M/d/yy h:mm"));
			char buf[1024];
			taglist_get_tagstring(mydive->tag_list, buf, 1024);
			notesBackup[mydive].tags = QString(buf);

			// maybe this is a place for memset?
			for (int i = 0; i < MAX_CYLINDERS; i++) {
				notesBackup[mydive].cylinders[i] = mydive->cylinder[i];
			}
			for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
				notesBackup[mydive].weightsystem[i] = mydive->weightsystem[i];
			}
		}
		editMode = DIVE;
	}
}

bool MainTab::eventFilter(QObject* object, QEvent* event)
{
	if (isEnabled() && event->type() == QEvent::KeyPress && object == ui.dateTimeEdit) {
		tabBar()->setTabIcon(currentIndex(), QIcon(":warning"));
		enableEdition();
	}

	if (isEnabled() && event->type() == QEvent::FocusIn && (object == ui.rating ||
								object == ui.visibility	||
								object == ui.tagWidget)) {
		tabBar()->setTabIcon(currentIndex(), QIcon(":warning"));
		enableEdition();
	}

	if (isEnabled() && event->type() == QEvent::MouseButtonPress ) {
		tabBar()->setTabIcon(currentIndex(), QIcon(":warning"));
		enableEdition();
	}
	return false; // don't "eat" the event.
}

void MainTab::clearEquipment()
{
	cylindersModel->clear();
	weightModel->clear();
}

void MainTab::clearInfo()
{
	ui.sacText->clear();
	ui.otuText->clear();
	ui.oxygenHeliumText->clear();
	ui.gasUsedText->clear();
	ui.dateText->clear();
	ui.diveTimeText->clear();
	ui.surfaceIntervalText->clear();
	ui.maximumDepthText->clear();
	ui.averageDepthText->clear();
	ui.waterTemperatureText->clear();
	ui.airTemperatureText->clear();
	ui.airPressureText->clear();
	ui.salinityText->clear();
	ui.tagWidget->clear();
}

void MainTab::clearStats()
{
	ui.depthLimits->clear();
	ui.sacLimits->clear();
	ui.divesAllText->clear();
	ui.tempLimits->clear();
	ui.totalTimeAllText->clear();
	ui.timeLimits->clear();
}

#define UPDATE_TEXT(d, field)				\
	if (!d || !d->field)				\
		ui.field->setText("");			\
	else						\
		ui.field->setText(d->field)

#define UPDATE_TEMP(d, field)				\
	if (!d || d->field.mkelvin == 0)		\
		ui.field->setText("");			\
	else						\
		ui.field->setText(get_temperature_string(d->field, TRUE))

void MainTab::updateDiveInfo(int dive)
{
	if (!isEnabled() && dive != -1)
		setEnabled(true);
	if (isEnabled() && dive == -1)
		setEnabled(false);
	editMode = NONE;
	// This method updates ALL tabs whenever a new dive or trip is
	// selected.
	// If exactly one trip has been selected, we show the location / notes
	// for the trip in the Info tab, otherwise we show the info of the
	// selected_dive
	volume_t sacVal;
	temperature_t temp;
	struct dive *prevd;
	struct dive *d = get_dive(dive);

	process_selected_dives();
	process_all_dives(d, &prevd);

	UPDATE_TEXT(d, notes);
	UPDATE_TEXT(d, location);
	UPDATE_TEXT(d, suit);
	UPDATE_TEXT(d, divemaster);
	UPDATE_TEXT(d, buddy);
	UPDATE_TEMP(d, airtemp);
	UPDATE_TEMP(d, watertemp);
	if (d) {
		ui.coordinates->setText(printGPSCoords(d->latitude.udeg, d->longitude.udeg));
		ui.dateTimeEdit->setDateTime(QDateTime::fromTime_t(d->when - gettimezoneoffset()));
		if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
			// only use trip relevant fields
			ui.coordinates->setVisible(false);
			ui.divemaster->setVisible(false);
			ui.DivemasterLabel->setVisible(false);
			ui.buddy->setVisible(false);
			ui.BuddyLabel->setVisible(false);
			ui.suit->setVisible(false);
			ui.SuitLabel->setVisible(false);
			ui.rating->setVisible(false);
			ui.RatingLabel->setVisible(false);
			ui.visibility->setVisible(false);
			ui.visibilityLabel->setVisible(false);
			// rename the remaining fields and fill data from selected trip
			dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
			ui.LocationLabel->setText(tr("Trip Location"));
			ui.location->setText(currentTrip->location);
			ui.NotesLabel->setText(tr("Trip Notes"));
			ui.notes->setText(currentTrip->notes);
		} else {
			// make all the fields visible writeable
			ui.coordinates->setVisible(true);
			ui.divemaster->setVisible(true);
			ui.buddy->setVisible(true);
			ui.suit->setVisible(true);
			ui.SuitLabel->setVisible(true);
			ui.rating->setVisible(true);
			ui.RatingLabel->setVisible(true);
			ui.visibility->setVisible(true);
			ui.visibilityLabel->setVisible(true);
			ui.BuddyLabel->setVisible(true);
			ui.DivemasterLabel->setVisible(true);
			/* and fill them from the dive */
			ui.rating->setCurrentStars(d->rating);
			ui.visibility->setCurrentStars(d->visibility);
			// reset labels in case we last displayed trip notes
			ui.LocationLabel->setText(tr("Location"));
			ui.NotesLabel->setText(tr("Notes"));
		}
		ui.maximumDepthText->setText(get_depth_string(d->maxdepth, TRUE));
		ui.averageDepthText->setText(get_depth_string(d->meandepth, TRUE));
		ui.otuText->setText(QString("%1").arg(d->otu));
		ui.waterTemperatureText->setText(get_temperature_string(d->watertemp, TRUE));
		ui.airTemperatureText->setText(get_temperature_string(d->airtemp, TRUE));
		ui.gasUsedText->setText(get_volume_string(get_gas_used(d), TRUE));
		ui.oxygenHeliumText->setText(get_gaslist(d));
		ui.dateText->setText(get_short_dive_date_string(d->when));
		ui.diveTimeText->setText(QString::number((int)((d->duration.seconds + 30) / 60)));
		if (prevd)
			ui.surfaceIntervalText->setText(get_time_string(d->when - (prevd->when + prevd->duration.seconds), 4));
		if ((sacVal.mliter = d->sac) > 0)
			ui.sacText->setText(get_volume_string(sacVal, TRUE).append(tr("/min")));
		else
			ui.sacText->clear();
		if (d->surface_pressure.mbar)
			/* this is ALWAYS displayed in mbar */
			ui.airPressureText->setText(QString("%1mbar").arg(d->surface_pressure.mbar));
		else
			ui.airPressureText->clear();
		if (d->salinity)
			ui.salinityText->setText(QString("%1g/l").arg(d->salinity/10.0));
		else
			ui.salinityText->clear();
		ui.depthLimits->setMaximum(get_depth_string(stats_selection.max_depth, TRUE));
		ui.depthLimits->setMinimum(get_depth_string(stats_selection.min_depth, TRUE));
		ui.depthLimits->setAverage(get_depth_string(stats_selection.avg_depth, TRUE));
		ui.sacLimits->setMaximum(get_volume_string(stats_selection.max_sac, TRUE).append(tr("/min")));
		ui.sacLimits->setMinimum(get_volume_string(stats_selection.min_sac, TRUE).append(tr("/min")));
		ui.sacLimits->setAverage(get_volume_string(stats_selection.avg_sac, TRUE).append(tr("/min")));
		ui.divesAllText->setText(QString::number(stats_selection.selection_size));
		temp.mkelvin = stats_selection.max_temp;
		ui.tempLimits->setMaximum(get_temperature_string(temp, TRUE));
		temp.mkelvin = stats_selection.min_temp;
		ui.tempLimits->setMinimum(get_temperature_string(temp, TRUE));
		if (stats_selection.combined_temp && stats_selection.combined_count) {
			const char *unit;
			get_temp_units(0, &unit);
			ui.tempLimits->setAverage(QString("%1%2").arg(stats_selection.combined_temp / stats_selection.combined_count, 0, 'f', 1).arg(unit));
		}
		ui.totalTimeAllText->setText(get_time_string(stats_selection.total_time.seconds, 0));
		int seconds = stats_selection.total_time.seconds;
		if (stats_selection.selection_size)
			seconds /= stats_selection.selection_size;
		ui.timeLimits->setAverage(get_time_string(seconds, 0));
		ui.timeLimits->setMaximum(get_time_string(stats_selection.longest_time.seconds, 0));
		ui.timeLimits->setMinimum(get_time_string(stats_selection.shortest_time.seconds, 0));


		char buf[1024];
		taglist_get_tagstring(d->tag_list, buf, 1024);
		ui.tagWidget->setText(QString(buf));

		multiEditEquipmentPlaceholder = *d;
		cylindersModel->setDive(&multiEditEquipmentPlaceholder);
		weightModel->setDive(&multiEditEquipmentPlaceholder);
	} else {
		/* clear the fields */
		clearInfo();
		clearStats();
		clearEquipment();
		ui.rating->setCurrentStars(0);
		ui.coordinates->clear();
		ui.visibility->setCurrentStars(0);
		/* turns out this is non-trivial for a dateTimeEdit... this is a partial hack */
		QLineEdit *le = ui.dateTimeEdit->findChild<QLineEdit*>();
		le->setText("");
	}
}

void MainTab::addCylinder_clicked()
{
	if(editMode == NONE)
		enableEdition();
	cylindersModel->add();
}

void MainTab::addWeight_clicked()
{
	if(editMode == NONE)
		enableEdition();
	weightModel->add();
}

void MainTab::reload()
{
	SuitCompletionModel::instance()->updateModel();
	BuddyCompletionModel::instance()->updateModel();
	LocationCompletionModel::instance()->updateModel();
	DiveMasterCompletionModel::instance()->updateModel();
	TagCompletionModel::instance()->updateModel();
}

void MainTab::acceptChanges()
{
	mainWindow()->dive_list()->setEnabled(true);
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment
	ui.diveNotesMessage->animatedHide();
	ui.diveEquipmentMessage->animatedHide();
	ui.notesButtonBox->hide();
	ui.equipmentButtonBox->hide();
	/* now figure out if things have changed */
	if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		if (notesBackup[NULL].notes != ui.notes->toPlainText() ||
			notesBackup[NULL].location != ui.location->text())
			mark_divelist_changed(TRUE);
	} else {
		struct dive *curr = current_dive;
		//Reset coordinates field, in case it contains garbage.
		ui.coordinates->setText(printGPSCoords(current_dive->latitude.udeg, current_dive->longitude.udeg));
		if (notesBackup[curr].buddy != ui.buddy->text() ||
			notesBackup[curr].suit != ui.suit->text() ||
			notesBackup[curr].notes != ui.notes->toPlainText() ||
			notesBackup[curr].divemaster != ui.divemaster->text() ||
			notesBackup[curr].location  != ui.location->text() ||
			notesBackup[curr].coordinates != ui.coordinates->text() ||
			notesBackup[curr].rating != ui.visibility->currentStars() ||
			notesBackup[curr].airtemp != ui.airtemp->text() ||
			notesBackup[curr].watertemp != ui.watertemp->text() ||
			notesBackup[curr].datetime != ui.dateTimeEdit->dateTime().toString(QString("M/d/yy h:mm")) ||
			notesBackup[curr].visibility != ui.rating->currentStars() ||
			notesBackup[curr].tags != ui.tagWidget->text()) {
			mark_divelist_changed(TRUE);
		}
		if (notesBackup[curr].location != ui.location->text() ||
			notesBackup[curr].coordinates != ui.coordinates->text()) {
			mainWindow()->globe()->reload();
		}

		if (notesBackup[curr].tags != ui.tagWidget->text())
			saveTags();

		if (cylindersModel->changed) {
			mark_divelist_changed(TRUE);
			Q_FOREACH (dive *d, notesBackup.keys()) {
				for (int i = 0; i < MAX_CYLINDERS; i++) {
					d->cylinder[i] = multiEditEquipmentPlaceholder.cylinder[i];
				}
			}
		}

		if (weightModel->changed) {
			mark_divelist_changed(TRUE);
			Q_FOREACH (dive *d, notesBackup.keys()) {
				for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
					d->weightsystem[i] = multiEditEquipmentPlaceholder.weightsystem[i];
				}
			}
		}

	}
	if (editMode == ADD) {
		// clean up the dive data (get duration, depth information from samples)
		fixup_dive(current_dive);
		if (dive_table.nr == 1)
			current_dive->number = 1;
		else if (current_dive == get_dive(dive_table.nr - 1) && get_dive(dive_table.nr - 2)->number)
			current_dive->number = get_dive(dive_table.nr - 2)->number + 1;
		DivePlannerPointsModel::instance()->cancelPlan();
		mainWindow()->showProfile();
		mainWindow()->refreshDisplay();
		mark_divelist_changed(TRUE);
	}
	editMode = NONE;

	resetPallete();
	mainWindow()->refreshDisplay();
}

void MainTab::resetPallete()
{
	QPalette p;
	ui.buddy->setPalette(p);
	ui.notes->setPalette(p);
	ui.location->setPalette(p);
	ui.coordinates->setPalette(p);
	ui.divemaster->setPalette(p);
	ui.suit->setPalette(p);
	ui.airtemp->setPalette(p);
	ui.watertemp->setPalette(p);
	ui.dateTimeEdit->setPalette(p);
	ui.tagWidget->setPalette(p);
}

#define EDIT_TEXT2(what, text) \
	textByteArray = text.toLocal8Bit(); \
	free(what);\
	what = strdup(textByteArray.data());

#define EDIT_TEXT(what, text) \
	QByteArray textByteArray = text.toLocal8Bit(); \
	free(what);\
	what = strdup(textByteArray.data());

void MainTab::rejectChanges()
{
	tabBar()->setTabIcon(0, QIcon()); // Notes
	tabBar()->setTabIcon(1, QIcon()); // Equipment

	mainWindow()->dive_list()->setEnabled(true);
	if (mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1){
		ui.notes->setText(notesBackup[NULL].notes );
		ui.location->setText(notesBackup[NULL].location);
	}else{
		if (editMode == ADD) {
			// clean up
			delete_single_dive(selected_dive);
			DivePlannerPointsModel::instance()->cancelPlan();
		}
		struct dive *curr = current_dive;
		ui.notes->setText(notesBackup[curr].notes );
		ui.location->setText(notesBackup[curr].location);
		ui.coordinates->setText(notesBackup[curr].coordinates);
		ui.buddy->setText(notesBackup[curr].buddy);
		ui.suit->setText(notesBackup[curr].suit);
		ui.divemaster->setText(notesBackup[curr].divemaster);
		ui.rating->setCurrentStars(notesBackup[curr].rating);
		ui.visibility->setCurrentStars(notesBackup[curr].visibility);
		ui.airtemp->setText(notesBackup[curr].airtemp);
		ui.watertemp->setText(notesBackup[curr].watertemp);
		ui.dateTimeEdit->setDateTime(QDateTime::fromString(notesBackup[curr].datetime, QString("M/d/y h:mm")));
		ui.tagWidget->setText(notesBackup[curr].tags);

		struct dive *mydive;
		for (int i = 0; i < dive_table.nr; i++) {
			mydive = get_dive(i);
			if (!mydive)
				continue;
			if (!mydive->selected)
				continue;

			QByteArray textByteArray;
			EDIT_TEXT2(mydive->buddy, notesBackup[mydive].buddy);
			EDIT_TEXT2(mydive->suit, notesBackup[mydive].suit);
			EDIT_TEXT2(mydive->notes, notesBackup[mydive].notes);
			EDIT_TEXT2(mydive->divemaster, notesBackup[mydive].divemaster);
			EDIT_TEXT2(mydive->location, notesBackup[mydive].location);
			mydive->latitude = notesBackup[mydive].latitude;
			mydive->longitude = notesBackup[mydive].longitude;
			mydive->rating = notesBackup[mydive].rating;
			mydive->visibility = notesBackup[mydive].visibility;

			// maybe this is a place for memset?
			for (int i = 0; i < MAX_CYLINDERS; i++) {
				mydive->cylinder[i] = notesBackup[mydive].cylinders[i];
			}
			for (int i = 0; i < MAX_WEIGHTSYSTEMS; i++) {
				mydive->weightsystem[i] = notesBackup[mydive].weightsystem[i];
			}
		}
		if (selected_dive > 0) {
			multiEditEquipmentPlaceholder = *get_dive(selected_dive);
			cylindersModel->setDive(&multiEditEquipmentPlaceholder);
			weightModel->setDive(&multiEditEquipmentPlaceholder);
		} else {
			cylindersModel->clear();
			weightModel->clear();
		}
	}

	ui.diveNotesMessage->animatedHide();
	ui.diveEquipmentMessage->animatedHide();
	mainWindow()->dive_list()->setEnabled(true);
	ui.notesButtonBox->hide();
	ui.equipmentButtonBox->hide();
	notesBackup.clear();
	resetPallete();
	if (editMode == ADD) {
		// more clean up
		updateDiveInfo(selected_dive);
		mainWindow()->showProfile();
		mainWindow()->refreshDisplay();
	}
	editMode = NONE;
}
#undef EDIT_TEXT2

#define EDIT_SELECTED_DIVES( WHAT ) do { \
	if (editMode == NONE) \
		return; \
\
	for (int i = 0; i < dive_table.nr; i++) { \
		struct dive *mydive = get_dive(i); \
		if (!mydive) \
			continue; \
		if (!mydive->selected) \
			continue; \
\
		WHAT; \
	} \
} while(0)

void markChangedWidget(QWidget *w){
	QPalette p;
	p.setBrush(QPalette::Base, QColor(Qt::yellow).lighter());
	w->setPalette(p);
}

void MainTab::on_buddy_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->buddy, text) );
	markChangedWidget(ui.buddy);
}

void MainTab::on_divemaster_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->divemaster, text) );
	markChangedWidget(ui.divemaster);
}

void MainTab::on_airtemp_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( mydive->airtemp.mkelvin = parseTemperatureToMkelvin(text) );
	markChangedWidget(ui.airtemp);
}

void MainTab::on_watertemp_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( mydive->watertemp.mkelvin = parseTemperatureToMkelvin(text) );
	markChangedWidget(ui.watertemp);
}

void MainTab::on_dateTimeEdit_dateTimeChanged(const QDateTime& datetime)
{
	EDIT_SELECTED_DIVES( mydive->when = datetime.toTime_t() + gettimezoneoffset() );
	markChangedWidget(ui.dateTimeEdit);
}

void MainTab::saveTags()
{
	EDIT_SELECTED_DIVES(
		QString tag;
		taglist_clear(mydive->tag_list);
		foreach (tag, ui.tagWidget->getBlockStringList())
			taglist_add_tag(mydive->tag_list, tag.toAscii().data());
	);
}

void MainTab::on_tagWidget_textChanged()
{
	markChangedWidget(ui.tagWidget);
}

void MainTab::on_location_textChanged(const QString& text)
{
	if (editMode == NONE)
		return;
	if (editMode == TRIP && mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		// we are editing a trip
		dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
		EDIT_TEXT(currentTrip->location, text);
	} else if (editMode == DIVE || editMode == ADD){
		if (!ui.coordinates->isModified() ||
		    ui.coordinates->text().trimmed().isEmpty()) {
			struct dive* dive;
			int i = 0;
			for_each_dive(i, dive){
				QString location(dive->location);
				if (location == text &&
				    (dive->latitude.udeg || dive->longitude.udeg)) {
					EDIT_SELECTED_DIVES( mydive->latitude = dive->latitude );
					EDIT_SELECTED_DIVES( mydive->longitude = dive->longitude );
					ui.coordinates->setText(printGPSCoords(dive->latitude.udeg, dive->longitude.udeg));
					markChangedWidget(ui.coordinates);
					break;
				}
			}
		}
		EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->location, text) );
	}

	markChangedWidget(ui.location);
}

void MainTab::on_suit_textChanged(const QString& text)
{
	EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->suit, text) );
	markChangedWidget(ui.suit);
}

void MainTab::on_notes_textChanged()
{
	if (editMode == NONE)
		return;
	if (editMode == TRIP && mainWindow() && mainWindow()->dive_list()->selectedTrips.count() == 1) {
		// we are editing a trip
		dive_trip_t *currentTrip = *mainWindow()->dive_list()->selectedTrips.begin();
		EDIT_TEXT(currentTrip->notes, ui.notes->toPlainText());
	} else if (editMode == DIVE || editMode == ADD) {
		EDIT_SELECTED_DIVES( EDIT_TEXT(mydive->notes, ui.notes->toPlainText()) );
	}
	markChangedWidget(ui.notes);
}

#undef EDIT_TEXT

void MainTab::on_coordinates_textChanged(const QString& text)
{
	bool gpsChanged = FALSE;
	EDIT_SELECTED_DIVES(gpsChanged |= gpsHasChanged(mydive, NULL, text));
	if (gpsChanged) {
		markChangedWidget(ui.coordinates);
	} else {
		QPalette p;
		p.setBrush(QPalette::Base, QColor(Qt::red).lighter());
		ui.coordinates->setPalette(p);
	}
}

void MainTab::on_rating_valueChanged(int value)
{
	EDIT_SELECTED_DIVES(mydive->rating  = value );
}

void MainTab::on_visibility_valueChanged(int value)
{
	EDIT_SELECTED_DIVES( mydive->visibility = value );
}

void MainTab::editCylinderWidget(const QModelIndex& index)
{
	if (editMode == NONE)
		enableEdition();

	if (index.isValid() && index.column() != CylindersModel::REMOVE)
		ui.cylinders->edit(index);
}

void MainTab::editWeightWidget(const QModelIndex& index)
{
	if (editMode == NONE)
		enableEdition();

	if (index.isValid() && index.column() != WeightModel::REMOVE)
		ui.weights->edit(index);
}

QString MainTab::printGPSCoords(int lat, int lon)
{
	unsigned int latdeg, londeg;
	unsigned int ilatmin, ilonmin;
	QString lath, lonh, result;

	if (!lat && !lon)
		return QString("");

	lath = lat >= 0 ? tr("N") : tr("S");
	lonh = lon >= 0 ? tr("E") : tr("W");
	lat = abs(lat);
	lon = abs(lon);
	latdeg = lat / 1000000;
	londeg = lon / 1000000;
	ilatmin = (lat % 1000000) * 60;
	ilonmin = (lon % 1000000) * 60;
	result.sprintf("%s%u%s %2d.%05d\' , %s%u%s %2d.%05d\'",
		       lath.toLocal8Bit().data(), latdeg, UTF8_DEGREE, ilatmin / 1000000, (ilatmin % 1000000) / 10,
		       lonh.toLocal8Bit().data(), londeg, UTF8_DEGREE, ilonmin / 1000000, (ilonmin % 1000000) / 10);
	return result;
}
