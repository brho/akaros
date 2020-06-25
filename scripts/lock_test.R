#!/usr/bin/env Rscript
#
# brho: 2014-10-13, 2020-06-25
#
# to install helper libraries, run R, install manually:
#
# 	install.packages('oce')
# 	install.packages('optparse')
# 	install.packages('data.table')
# didn't need to library()-load this, but data.table wanted it at one point
# 	install.packages('bit64')
#
# To run it, you can do stuff like this:
#
#  for i in *.dat.gz; do
#          echo Processing $i...
#          $AKA_DIR/scripts/lock_test.R -c 1 $i
#  done
#
#  echo "Creating ${DIR%/}-tput.pdf"
#  $AKA_DIR/scripts/lock_test.R -c 2 *.dat.gz -o ${DIR%/}-tput.pdf
#
#  echo "Creating ${DIR%/}-kernel-tput.pdf"
#  $AKA_DIR/scripts/lock_test.R -c 2 *kernel*.dat.gz -o ${DIR%/}-kernel-tput.pdf
#
# TODO:
# - analyze when the lock jumps between locality regions (hyperthread,
# core, CCX, numa, whatever).  It's a bit of a pain with Linux, since core 1 is
# on another numa node than core 0.  Whatever.
# - For lock_test, have each additional core/thread be allocated close to the
# existing core (locality), and then look at the lock scalability.  (classic,
# X-axis is nr_cpus, Y axis is time per lock (avg acq lat)).
# - Figure out if there was something special about disable/enable IRQs

# library that includes the pwelch function
suppressPackageStartupMessages(library(oce))
# library for command line option parsing
suppressPackageStartupMessages(library(optparse))
# read.table is brutally slow.  this brings in fread
suppressPackageStartupMessages(library(data.table))

# file format: thread_id attempt pre acq(uire) un(lock) tsc_overhead
# 0 0 4748619790389387 4748619790429260 4748619790429323 0

g_tsc_overhead <- 0
g_tsc_frequency <- 0

# For PNG output...
g_width <- 1920
g_height <- 1200

######################################
### Functions
######################################

# takes any outliers 2 * farther than the 99th quantile and rounds them down to
# that limit.  the limit is pretty arbitrary.  useful for not having
# ridiculously large graphs, but still is lousy for various datasets.
round_outlier <- function(vec)
{
	vec99 = quantile(vec, .99)
	lim = vec99 + 2 * (vec99 - median(vec))
	return(sapply(vec, function(x) min(x, lim)))
}

# computes acquire latency, using global tsc freq if there isn't one in the
# data
acq_latency <- function(data)
{
	tsc_overhead = data$V6
	if (tsc_overhead[1] == 0)
		tsc_overhead = sapply(tsc_overhead, function(x) g_tsc_overhead)
	return (data$V4 - data$V3 - tsc_overhead)
}

# computes hold latency, using global tsc freq if there isn't one in the data
hld_latency <- function(data)
{
	tsc_overhead = data$V6
	if (tsc_overhead[1] == 0)
		tsc_overhead = sapply(tsc_overhead, function(x) g_tsc_overhead)
	return (data$V5 - data$V4 - tsc_overhead)
}

# histogram, bins based on percentiles, with limits of the graph based on the
# outermost bins.  somewhat works.  can get a 'need finite ylim' if the bins
# are too small.  maybe since there are no values in it.
#
# with density and percentiles for bins, keep in mind the area of a rectangle
# is the fraction of data points in the cell.  since all bins have the same
# amount of data points, taller cells show a denser concentration in a skinnier
# bin
#
# i don't actually like this much.  using a round_outlier with 20-bin hist or a
# density plot look nicer.
quant_hist <- function(vec)
{
	vec_quant = c(quantile(vec, probs=seq(0, 1, .01)))
	print(vec_quant)
	# keep the 100 in sync with the 0.01 above
	hist(vec, breaks=vec_quant, xlim=c(vec_quant[2], vec_quant[100]))
}

# line_data is a list of N data sets that can be plotted as lines.  each set
# should just be an array of values; the index will be the X value.
#
# names is a c() of N strings corresponding to the line_data members
# Set the desired x and y min/max.
plot_lines <- function(line_data, names=NULL, outfile="", title="",
		       xlab="X", ylab="Y", min_x=0, max_x=0, min_y=0, max_y=0)
{
	nr_lines <- length(line_data)

	# not guaranteed to work, but might save some pain
	if (max_x == 0) {
		for (i in 1:nr_lines)
			max_x <- max(max_x, length(line_data[[i]]))
	}
	if (max_y == 0) {
		for (i in 1:nr_lines)
			max_y <- max(max_y, max(line_data[[i]]))
	}

	# http://www.statmethods.net/graphs/line.html
	colors <- rainbow(nr_lines) # not a huge fan.  color #2 is light blue.
	linetype <- c(1:nr_lines)
	plotchar <- seq(18, 18 + nr_lines, 1)

	# http://stackoverflow.com/questions/8929663/r-legend-placement-in-a-plot
	# can manually move it if we don't want to waste space
	if (!is.null(names)) {
		plot(c(min_x, max_x), c(min_y, max_y), type="n", xaxt="n", yaxt="n")
		legend_sz = legend("topright", legend=names, lty=linetype,
		                   plot=FALSE)
		max_y = 1.04 * (max_y + legend_sz$rect$h)
	}

	if (outfile != "")
		png(outfile, width=g_width, height=g_height)

	plot(c(min_x, max_x), c(min_y, max_y), type="n", main=title, xlab=xlab,
		ylab=ylab)

	for (i in 1:nr_lines) {
		# too many points, so using "l" and no plotchar.
		#lines(densities[[i]], type="b", lty=linetype[i], col=colors[i],
		#      pch=plotchar[i], lwd=1.5)
		lines(line_data[[i]], type="l", lty=linetype[i], col=colors[i],
			lwd=1.5)
	}

	#legend(x=min_x, y=max_y, legend=names, lty=linetype, col=colors)
	if (!is.null(names))
		legend("topright", legend=names, lty=linetype, col=colors)

	if (outfile != "")
		invisible(dev.off())
}

plot_densities <- function(vecs, names=NULL, outfile="")
{
	nr_vecs = length(vecs)
	densities = list()
	max_y = 0
	min_x = Inf
	max_x = 0

	for (i in 1:nr_vecs) {
		# [[ ]] chooses the actual element.  [] just subsets
		dense_i = density(vecs[[i]])
		densities = c(densities, list(dense_i))
		max_y = max(max_y, dense_i$y)
		max_x = max(max_x, dense_i$x)
		min_x = min(min_x, dense_i$x)
	}
	plot_lines(densities, names=names, outfile=outfile,
	           title=paste(outfile, "Lock Acquisition Latency"),
		   xlab="TSC Ticks", ylab="PDF",
		   min_x=min_x, max_x=max_x, max_y=max_y)
}

plot_density <- function(vec, outfile="")
{
	vecs = list(vec)
	plot_densities(vecs=vecs, outfile=outfile)
}

plot_acq_times <- function(data, outfile="")
{
	if (outfile != "")
		png(outfile, width=g_width, height=g_height)

	# all acquire times, timestamps starting at 0
	time0 = min(data$V4)
	total_acq <- data$V4 - time0

	threadid <- unique(data$V1)

	acq_n <- list()
	names <- c()
	for (i in threadid) {
		thread_data <- subset(data, data$V1 == i) - time0
		acq_n <- c(acq_n, list(thread_data$V4))
		names <- c(names, paste("Thread ", i))
	}
	# can adjust ylim, default are from 1..nr_items
	stripchart(acq_n, group.names=names, pch='.', xlab="Time (TSC Ticks)",
	           main="Lock Acquisition Timestamps")

	if (outfile != "")
		invisible(dev.off())
}

print_vec <- function(vec, name)
{
	# goddamn.  cat doesn't like integer64 or something, so you need to
	# pre-paste for the calculations.  print will spit out [1] for row
	# names, which sucks.
	cat(name, "\n")
	cat("---------------\n")
	cat(paste("Average: ", round(mean(vec), 4), "\n"))
	cat(paste("Stddev: ", round(sd(vec), 4), "\n"))
	quants = round(quantile(vec, c(.5, .75, .9, .99, .999)))
	cat(paste("50/75/90/99/99.9: ", quants[[1]], quants[[2]], quants[[3]],
	      quants[[4]], quants[[5]], "\n"))
	cat(paste("Min: ", min(vec), " Max: ", max(vec), "\n"))
	cat("\n")
}

# using something like the tables package to output latex booktab's would be
# much nicer
print_stats <- function(data)
{
	print_vec(acq_latency(data), "Acquire Latency")
	print_vec(hld_latency(data), "Hold Latency")
}

# the 'queue' system includes waiting for the lock and holding the lock.
# Returns a data.frame, with X = TSC, Y = qlen
get_qlen <- function(data)
{
	if (g_tsc_frequency == 0)
		stop("WARNING: global TSC freq not set!")

	# timestamps for all threads, sorted.
	# a 'pre' is someone entering the q.  'unl' is leaving.
	# both are normalized to the TSC time for pres
	pres <- sort(data$V3 - min(data$V3))
	unls <- sort(data$V5 - min(data$V3))

	# not sure the best way to create the data to print.  easiest seems to
	# be two vectors, X = times, Y = qlens, which we'll pass to plot
	# but you can't append to them, that's way to slow.  we do know the
	# overall length of both: one entry for each of pres and unls.
	# so we preallocate.
	samples <- length(pres) + length(unls)

	times <- rep(0, samples)
	qlens <- rep(0, samples)

	qlen <- 0
	# R uses 1 indexing
	p_i <- 1
	u_i <- 1

	for (i in 1:samples) {
		now <- 0

		# a bit painful.
		if (p_i <= length(pres))
			pre <- pres[[p_i]]
		else
			pre <- Inf
		if (u_i <= length(unls))
			unl <- unls[[u_i]]
		else
			unl <- Inf

		if (pre <= unl) {
			if (pre == Inf) {
				print("both pre and unl were Inf!")
				break
			}

			qlen <- qlen + 1
			now <- pre
			p_i <- p_i + 1
		} else {
			if (unl == Inf) {
				print("both unl and pre were Inf!")
				break
			}

			qlen <- qlen - 1
			now <- unl
			u_i <- u_i + 1
		}
		times[i] = now
		qlens[i] = qlen
	}
	# times is in units of TSC.  convert to msec, for easier comparison
	# with throughput.  though note that when plotting with e.g. tput, the
	# x-axis is set by tput, and whatever we print is scaled to fit in that
	# space.  so if you didn't do this, you wouldn't notice.  (I was using
	# nsec for a while and didn't notice).
	times <- times / (g_tsc_frequency / 1e3)

	return(data.frame(setNames(list(times, qlens),
	                           c("Time (msec)", "Queue Length"))))
}

plot_qlen <- function(data, outfile="")
{
	df <- get_qlen(data)
	if (outfile != "")
		png(outfile, width=g_width, height=g_height)

	# df isn't a 'line', so we need to plot it directly
	plot(df, type="p", main="Spinlock Queue Length")

	if (outfile != "")
		invisible(dev.off())
}

get_tput <- function(data)
{
	if (g_tsc_frequency == 0)
		stop("WARNING: global TSC freq not set!")

	# acq times, sorted, normalized to the lowest, still in ticks
	total_acq <- sort(data$V4 - min(data$V4))

	# convert to nsec
	total_acq <- total_acq / (g_tsc_frequency / 1e9)

	# rounds down all times to the nearest msec, will collect into a table,
	# which counts the freq of each bucket, as per:
	# http://stackoverflow.com/questions/5034513/how-to-graph-requests-per-second-from-web-log-file-using-r
	msec_times <- trunc(total_acq/1e6)

	# if we just table directly, we'll lose the absent values (msec where no
	# timestamp happened).  not sure if factor is the best way, the help
	# says it should be a small range.
	# http://stackoverflow.com/questions/1617061/including-absent-values-in-table-results-in-r
	msec_times <- factor(msec_times, 0:max(msec_times))

	# without the c(), it'll be a bunch of bars at each msec
	tab <- c(table(msec_times))
	return(tab)
}

# this is a little rough.  You need the data loaded, but calculating tput
# requires the right TSC freq set.  So don't use this with data from different
# machines (which can be a tough comparison, regardless).
plot_tputs <- function(data_l, names=NULL, outfile="")
{
	nr_lines <- length(data_l)
	tputs <- list()

	for (i in 1:nr_lines) {
		# [[ ]] chooses the actual element.  [] just subsets
		tput_i <- get_tput(data_l[[i]])
		tputs <- c(tputs, list(tput_i))
	}

	plot_lines(tputs, names, outfile=outfile,
	           title=paste(outfile, "Lock Throughput"),
		   xlab = "Time (msec)", ylab = "Locks per msec")
}


# helper, plots throughput (tp) and whatever else you give it.  e.g. qlen is a
# value at a timestamp (msec)
plot_tput_foo <- function(tp, foo, foo_str, outfile="")
{
	if (outfile != "")
		png(outfile, width=g_width, height=g_height)

	# decent looking margins for the right Y axis
	par(mar = c(5, 5, 3, 5))

	# Scaling ylim to make room for FOO, which tends to be up top
	plot(tp, type="l", main=paste(outfile, "Lock Throughput and", foo_str),
	     ylim=c(0, max(tp)*1.2),
	     xlab="Time (msec)", ylab="Throughput",
	     col="blue", lty=1)

	par(new = TRUE)
	plot(foo, type="p", xaxt = "n", yaxt = "n", ylab = "", xlab = "",
	     col="red", lty=2)
	axis(side = 4)
	mtext(foo_str, side = 4)

	legend("topleft", c("tput", foo_str), col = c("blue", "red"),
	       lty = c(1, 2))

	if (outfile != "")
		invisible(dev.off())
}

# plot throughput and queue length on the same graph
plot_tput_qlen <- function(data, outfile="")
{
	tp <- get_tput(data)
	ql <- get_qlen(data)

	plot_tput_foo(tp, ql, "Qlen", outfile)
}

# if you know how many msec there are, this is like doing:
#     hist(total_acq/1000000, breaks=50)
# except it gives you a line, with points being the top of the hist bars
plot_tput <- function(data, outfile="")
{
	plot_tputs(list(data), outfile=outfile)
}

# the middle timestamp is the acquire-by-lockholder.  sorting on that, we can
# see the coreid.  it's a really busy thing to graph.
#
# You can see some gaps in non-MCS lock holders when a core is starved or not
# partipating.  By comparing that gap to the qlen, you can see if it was
# starvation or just not participating.  Also, you can easily spot a preempted
# lockholder (if they are gone for enough msec).
get_core_acqs <- function(data)
{
	earliest <- min(data$V3)
	sorted <- data[order(data$V3)]

	times <- sorted$V3 - earliest
	cores <- sorted$V1

	# times is in units of TSC.  convert to msec, for easier comparison
	# with throughput
	times <- times / (g_tsc_frequency / 1e3)

	return(data.frame(setNames(list(times, cores),
	                           c("Time (msec)", "Holder ID"))))
}

plot_tput_core_acqs <- function(data, outfile="")
{
	tp <- get_tput(data)
	ca <- get_core_acqs(data)

	plot_tput_foo(tp, ca, "Holder ID", outfile)
}

get_tsc_freq <- function(filename) {
	# format looks like this:
	# tsc_frequency 2300002733
	header <- readLines(filename, n=50)
	line <- grep("tsc_frequency", header, value=TRUE)
	x <- strsplit(line, split=" ")
	return(as.numeric(x[[1]][3]))
}


# pretty ugly.  reads in the data, but also sets the TSC freq, which is
# particular to a given machine.  so if you use this, be sure you do any
# analyses before loading another.  Sorry.
load_test_data <- function(filename)
{
	g_tsc_frequency <- get_tsc_freq(filename)
	if (g_tsc_frequency == 0)
		stop("WARNING: could not set global TSC freq!")
	assign("g_tsc_frequency", g_tsc_frequency, envir = .GlobalEnv)

	# using grep for a hokey comment.char="#".  it's fast enough for now.

	# integer64="numeric", o/w all sorts of things fuck up.  like density()
	# and round_outlier().  Even with sprinkling as.numeric() around, it's
	# still a mess.  This actually worked, though presumably there's a
	# reason to use int64.

	if (grepl("\\.gz$", filename)) {
		return(fread(cmd=paste("gunzip -c", filename, "| grep -v '^#'"),
		             integer64="numeric"))
	} else {
		return(fread(cmd=paste("grep -v '^#'", filename),
		             integer64="numeric"))
	}
}

######################################
### Main
######################################
# establish optional arguments
# "-h" and "--help" are automatically in the list
option_list <- list(
	make_option(c("-c", "--cmd"), type="integer", default=-1,
	            help="
	1: acq_lat & tput_qlen for one file
	2: tput comparison for multiple files
	"),
	make_option(c("-o", "--output"), type="character",
	            default="", help="Output file, if applicable")
)

# CMD 1
single_analysis <- function(args)
{
	data_file <- args$args

	if (file.access(data_file) == -1)
		stop("cannot access input file")

	data <- load_test_data(data_file)

	basename <- data_file
	basename <- gsub(".gz$", "", basename)
	basename <- gsub(".dat$", "", basename)

	sink(paste(basename, "-stats.txt", sep=""))
	cat("Filename:", data_file, "\n")
	cat("\n")
	print(grep("^#", readLines(data_file, n=50), value=TRUE))
	cat("\n")
	print_stats(data)
	sink()

	# For now, create all files.  Can make options later
	plot_density(round_outlier(acq_latency(data)),
	             outfile=paste(basename, "-acq.png", sep=""))
	plot_tput_qlen(data,
	               outfile=paste(basename, "-tput_qlen.png", sep=""))
	plot_tput_core_acqs(data,
	               outfile=paste(basename, "-tput_core_acqs.png", sep=""))
}

# CMD 2
tput_comparison <- function(args)
{
	outfile <- args$options$output

	if (outfile == "")
		stop("Need an outfile (-o)")

	tputs <- list()
	names <- list()

	for (i in args$args) {
		data <- load_test_data(i)
		# I wanted to do this:
		#tputs <- append(tputs, get_tput(data))
		# but it's a collection of lists of TPUT tables.  or something
		tputs <- c(tputs, list(get_tput(data)))
		names <- append(names, i)
		rm(data)
	}
	plot_lines(tputs, names, outfile=outfile,
	           title="Lock Throughput",
		   xlab = "Time (msec)",
		   ylab = "Locks per msec")
}

main <- function()
{
	parser <- OptionParser(usage="%prog [options] data.dat",
	                       option_list=option_list)
	# ugly as hell, no error messages, etc.
	args <- parse_args(parser, positional_arguments = TRUE)
	opt <- args$options

	f <- switch(opt$cmd, single_analysis, tput_comparison)
	if (is.null(f))
		stop("Need a valid (-c) command (-h for options)")
	f(args)
}

# don't run when sourced
if (sys.nframe() == 0L) {
	main()
}

# Dirty Hacks Storage

# source("scripts/lock_test.R")

# this is slow and shitty:
# raw = read.table(filename, comment.char="#")

# to print stuff to a file, you can do
# that can be really slow for large files, so try png instead
#pdf("foo.pdf")
#command that creates a graphic, like hist or plot
#dev.off()

# hack, so i can re-source this and not have to reload TSC freq
#g_tsc_frequency <- get_tsc_freq("raw1.dat")
