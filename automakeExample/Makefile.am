# gnu/gnits/foreign，gnu/gnits需要附带相应的许可证书，foreign不需要
AUTOMAKE_OPTIONS=foreign

# 指定包含的子目录，编译时会调用每个子目录下的Makefile
# SUBDIRS=src1 src2 src3
#
# 指定不需要安装但需要编译的库文件
# noninst_LIBRARIES=libxxx.a
#
# noninstdir=/usr/lib/libxxx.a
#
# ACLOCAL_AMFLAGS=-I m4 所有宏添加到m4目录

# 生成的可执行文件名，多个用空格分隔
bin_PROGRAMS=helloworld helloworld2
#bin_SCRIPTS=hellworld.sh

# 指定生成每个可执行文件需要的源文件，包括头文件
helloworld_SOURCES=src/test.c include/test.h
helloworld2_SOURCES=src/test.c include/test.h

# 若生成的目标文件为库文件，命名方式如下，其他以此类推
# libxxx_a_SOURCES=xxx.c xxx.h

# 指定依赖的库文件，这里不需要指定-lm
# top_srcdir顶层文件夹目录
# helloworld_LDADD=$(top_srcdir)/lib/libxxx.a

# 设置链接选项
# helloworld_LDFLAGS=-D_GNU_SOURCE -static
# DEFS+=-D_GNU_SOURCE
