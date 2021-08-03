#include <iomanip>
#include <iostream>
#include <sstream>

#include "CPU.h"
#include "Util.h"
#include "ui/HexView.h"
#include "ui/Util.h"

namespace RVGUI {
	HexView::HexView(int row_height, int cell_width):
	Gtk::Box(Gtk::Orientation::HORIZONTAL),
	rowHeight(row_height),
	cellWidth(cell_width),
	adjustment(Gtk::Adjustment::create(0, 0, 100, 1, 10, 0)),
	scrollbar(adjustment, Gtk::Orientation::VERTICAL) {
		set_overflow(Gtk::Overflow::HIDDEN);
		scrolled.set_child(grid);
		scrolled.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::EXTERNAL);
		append(scrolled);
		append(scrollbar);
		grid.set_hexpand(true);
		adjustment->signal_value_changed().connect(sigc::mem_fun(*this, &HexView::onScrolled));
		scrollController = Gtk::EventControllerScroll::create();
		scrollController->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
		scrollController->signal_scroll().connect(sigc::mem_fun(*this, &HexView::onScroll), false);
		grid.add_controller(scrollController);
		sizeLabel.add_css_class("hidden");
		grid.attach(sizeLabel, 0, 0);
		grid.set_overflow(Gtk::Overflow::HIDDEN);
		add_css_class("hexview");
	}

	HexView & HexView::setCPU(std::shared_ptr<CPU> cpu_) {
		cpu = cpu_;
		if (cpu)
			cpu->onByteUpdate = sigc::mem_fun(*this, &HexView::updateLabel);
		adjustment->set_value(0);
		reset();
		return *this;
	}

	void HexView::onResize() {
		reset();
	}

	void HexView::reset() {
		if (digitWidth == -1) {
			digitWidth  = sizeLabel.get_width();
			digitHeight = sizeLabel.get_height();
			if (digitWidth == 0) {
				digitWidth = -1;
				return;
			}
		}

		removeChildren(grid);
		widgets.clear();
		const int grid_width = grid.get_width();
		const size_t offset = static_cast<size_t>(std::floor(adjustment->get_value()));
		// Here's some needlessly complicated math. Some values are inaccurate because the values are interdependent.
		const int row_count = updiv(grid.get_height(), digitHeight);
		static constexpr int GUTTER_PADDING = 3;
		// offset + row_count should be multiplied by cells_per_row, but we don't know that yet!
		const int max_row = offset + row_count - 1;
		const int digit_count = 1 + (max_row == 0? 0 : static_cast<int>(std::log2(max_row) / 4)); // log16 = log2 / 4
		const int est_gutter_width = digit_count * (digitWidth + 1) + 2 * GUTTER_PADDING;
		if (grid_width < est_gutter_width + 2 + cellWidth)
			return;
		const int cells_per_row = (grid_width - est_gutter_width - 2) / (digitWidth * 3);
		if (cpu) {
			const size_t memory_size = cpu->memorySize();
			const CPU::Word pc = cpu->getPC();

			const double old_upper = adjustment->get_upper();
			const double new_upper = static_cast<double>(memory_size) / cells_per_row;
			if (old_upper != new_upper) {
				const double new_value = new_upper / old_upper * adjustment->get_value();
				adjustment->set_upper(new_upper);
				adjustment->set_value(new_value);
			}

			cellLabels.clear();

			for (int row = 0; row < row_count; ++row) {
				std::stringstream ss;
				ss.imbue(std::locale::classic()); // Avoid thousands separators
				const size_t row_offset = (offset + row) * cells_per_row;
				ss << std::right << std::hex << std::setw(digit_count) << std::setfill('0') << row_offset;
				auto &label = *widgets.emplace_back(new Gtk::Label(ss.str(), Gtk::Align::END));
				label.set_margin_start(GUTTER_PADDING);
				label.set_margin_end(GUTTER_PADDING);
				grid.attach(label, 0, row);
				auto &separator = *widgets.emplace_back(new Gtk::Separator(Gtk::Orientation::VERTICAL));
				grid.attach(separator, 1, row);
				for (int column = 0; column < cells_per_row; ++column) {
					size_t address = row_offset + column;
					auto &label = cellLabels.try_emplace(address, getLabel(address)).first->second;
					if (address / 8 == static_cast<size_t>(pc) / 8)
						label.add_css_class("pc");
					label.add_css_class("byte");
					grid.attach(label, 2 + column, row);
				}
			}
		}

		oldColumnCount = cells_per_row;
		oldOffset = offset * cells_per_row;
	}

	void HexView::updateLabel(uintptr_t cell, uint8_t value) {
		if (cellLabels.count(cell) != 0)
			cellLabels.at(cell).set_text(getLabel(cell));
	}

	std::string HexView::getLabel(uintptr_t cell) {
		std::stringstream ss;
		if (cpu->memorySize() <= cell)
			ss << "xx";
		else
			ss << std::right << std::hex << std::setw(2) << std::setfill('0')
			   << static_cast<int>((*cpu)[cell]);
		return ss.str();
	}

	bool HexView::onScroll(double, double dy) {
		adjustment->set_value(adjustment->get_value() + dy);
		return true;
	}

	void HexView::onScrolled() {
		reset();
		// double position = std::floor(adjustment->get_value());
	}
}
