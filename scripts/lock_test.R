# brho: 2014-10-13
#
# this is partly fleshed out.  to use, i've just been sourcing the script in R,
# then overriding the tsc overhead and freq.  then just running various
# functions directly, like print_stats, plot_densities, plot_tput, etc.  don't
# expect any command line options to work.

# library that includes the pwelch function
suppressPackageStartupMessages(library(oce))
# library for command line option parsing
suppressPackageStartupMessages(library(optparse))

# file format: thread_id attempt pre acq(uire) un(lock) tsc_overhead

g_tsc_overhead <- 0
g_tsc_frequency <- 0

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

plot_densities <- function(vecs, names=NULL, outfile="",
                           title="Lock Acquisition Latency",
                           xlab="TSC Ticks")
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

	# http://www.statmethods.net/graphs/line.html
	colors <- rainbow(nr_vecs) # not a huge fan.  color #2 is light blue.
	linetype <- c(1:nr_vecs)
	plotchar <- seq(18, 18 + nr_vecs, 1)

	# http://stackoverflow.com/questions/8929663/r-legend-placement-in-a-plot
	# can manually move it if we don't want to waste space
	if (!is.null(names)) {
		plot(c(min_x,max_x), c(0, max_y), type="n", xaxt="n", yaxt="n")
		legend_sz = legend("topright", legend=names, lty=linetype,
		                   plot=FALSE)
		max_y = 1.04 * (max_y + legend_sz$rect$h)
		invisible(dev.off())
	}

	if (outfile != "")
		pdf(outfile)

	plot(c(min_x,max_x), c(0, max_y), type="n", xlab=xlab, main=title,
	     ylab="Density")

	for (i in 1:nr_vecs) {
		# too many points, so using "l" and no plotchar.
		#lines(densities[[i]], type="b", lty=linetype[i], col=colors[i],
		#      pch=plotchar[i], lwd=1.5)
		lines(densities[[i]], type="l", lty=linetype[i], lwd=1.5)
	}

	#legend(x=min_x, y=max_y, legend=names, lty=linetype, col=colors)
	if (!is.null(names))
		legend("topright", legend=names, lty=linetype)

	if (outfile != "")
		invisible(dev.off())
}


plot_density <- function(vec, outfile="",
                         title="Lock Acquisition Latency",
                         xlab="TSC Ticks")
{
	vecs = list(vec)
	plot_densities(vecs=vecs, outfile=outfile, title=title, xlab=xlab)
}


plot_acq_times <- function(data, outfile="")
{
	if (outfile != "")
		pdf(outfile)

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

print_vec <- function(vec)
{
	# this whole str, paste dance is nasty
	print("---------------")
	str = paste("Average: ", round(mean(vec), 4))
	print(str)
	str = paste("Stddev: ", round(sd(vec), 4))
	print(str)
	quants = round(quantile(vec, c(.5, .75, .9, .99, .999)))
	str = paste("50/75/90/99/99.9: ", quants[[1]], quants[[2]], quants[[3]],
	            quants[[4]], quants[[5]])
	print(str)
	str = paste("Min: ", min(vec), " Max: ", max(vec))
	print(str)
}

# using something like the tables package to output latex booktab's would be
# much nicer
print_stats <- function(data)
{
	acq_lat = acq_latency(data)
	hld_lat = hld_latency(data)

	print("Acquire Latency")
	print_vec(acq_lat)
	print("")
	print("Hold Latency")
	print_vec(hld_lat)
}

# if you know how many msec there are, this is like doing:
#     hist(total_acq/1000000, breaks=50)
# except it gives you a line, with points being the top of the hist bars
plot_tput <- function(data, title="Lock Acquisition Throughput", outfile="")
{
	if (outfile != "")
		pdf(outfile)

	total_acq = sort(data$V4 - min(data$V4))

	if (g_tsc_frequency == 0)
		stop("WARNING: global TSC freq not set!")
	# convert to nsec? XXX
	total_acq = total_acq / (g_tsc_frequency / 1e9)

	# rounds down all times to the nearest msec, will collect into a table,
	# which counts the freq of each bucket, as per:
	# http://stackoverflow.com/questions/5034513/how-to-graph-requests-per-second-from-web-log-file-using-r
	msec_times = trunc(total_acq/1e6)

	# if we just table directly, we'll lose the absent values (msec where no
	# timestamp happened).  not sure if factor is the best way, the help
	# says it should be a small range.
	# http://stackoverflow.com/questions/1617061/including-absent-values-in-table-results-in-r
	msec_times = factor(msec_times, 0:max(msec_times))

	# without the c(), it'll be a bunch of bars at each msec
	tab = c(table(msec_times))
	plot(tab, type="o", main=title, xlab="Time (msec)",
	     ylab="Locks per msec")

	if (outfile != "")
		invisible(dev.off())
}


# extract useful information from the raw data file
extract_data <- function(filename) {
	mydata = read.table(filename, comment.char="#")

	work_amt = mydata$V2

	# calculate time steps and mean time step (all in ns)
	times = as.numeric(as.character(mydata$V1))
	N_entries = length(times)
	time_steps_ns = times[2:N_entries] - times[1:(N_entries-1)]
	avg_time_step_ns = mean(time_steps_ns)

	return(list(work_amt=work_amt, time_steps_ns=time_steps_ns,
		N_entries=N_entries, avg_time_step_ns=avg_time_step_ns))
}


######################################
### Main
######################################

### collect command line arguments
# establish optional arguments
# "-h" and "--help" are automatically in the list
option_list <- list(
  make_option(c("-i", "--input"), type="character",
    default="welch_input.dat",
    help="Input data file"),
  make_option(c("-o", "--output"), type="character",
    default="welch_plot.pdf",
    help="Output file for plotting"),
  make_option("--xmin", type="double", default=0,
    help=paste("Minimum frequency (horizontal axis) ",
      "in output plot [default %default]",sep="")),
  make_option("--xmax", type="double", default=40,
    help=paste("Maximum frequency (horizontal axis) ",
      "in output plot [default %default]",sep="")),
  make_option("--ymin", type="double", default=-1,
    help=paste("Minimum spectrum (vertical axis) ",
      "in output plot [default adaptive]",sep="")),
  make_option("--ymax", type="double", default=-1,
    help=paste("Maximum spectrum (vertical axis) ",
      "in output plot [default adaptive]",sep=""))
)

## read command line
#opt <- parse_args(OptionParser(option_list=option_list))
#  
##max_freq = as.numeric(as.character(args[3]))
#
#### read in data
#mydata = extract_data(opt$input)

#round_outlier <- function(vec)
#acq_latency <- function(data)
#hld_latency <- function(data)
#plot_densities <- function(vecs, names=NULL, outfile="",
#plot_density <- function(vec, outfile="",
#plot_acq_times <- function(data, outfile="")
#print_vec <- function(vec)
#print_stats <- function(data)
#plot_tput <- function(data)
#mydata = read.table(filename, comment.char="#")

