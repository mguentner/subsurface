#ifndef PRINTLAYOUT_H
#define PRINTLAYOUT_H

#include <QPrinter>
#include <QPainter>
#include "../display.h"

class PrintDialog;

class PrintLayout : public QObject {
	Q_OBJECT

public:
	PrintLayout(PrintDialog *, QPrinter *, struct options *);
	void print();

private:
	PrintDialog *dialog;
	QPrinter *printer;
	struct options *printOptions;

	QPainter painter;

	void printSixDives();
	void printTwoDives();
	void printTable();
};

#endif