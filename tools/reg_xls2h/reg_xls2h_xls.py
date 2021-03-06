########################################################################################
#
# "xls" class definition
#
#    Copyright (C) 2009 Louis Caron
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
########################################################################################
import xlrd
import re
import copy
import struct

from reg_xls2h import *

from common.legalexception import *

# supported access rights (X means unknown)
hw_rights = ('X', 'R/W', 'R', 'W')
sw_rights = ('X', 'R/W', 'R', 'W', 'S', 'C')
# list of writable rights
sw_writable = ('X', 'R/W', 'W', 'S', 'C')

# compatible register SW rights with field rights
compat = {'C':('C', 'X'),
          'S':('S', 'X'),
          'R/W':('R/W', 'X'),
          'W':('W', 'X'),
          'R':('R/W', 'R', 'S', 'C', 'X')}


class xls_field:
    "class of an xls field definition"
    def __init__(self, name, high_bitpos, high_index, hw, sw, verbose=False):
        "constructor"

        self.verbose = verbose

        # check the name of the register
        result = re.compile("^\s*(\S+)\s*$").search(name)
        if result == None:
            raise LegalException("""ERROR: field "%s", name format is not parsable"""%(name, ))

        # save the name of the field
        self.name = name

        # save the information
        self.high_bitpos = high_bitpos
        self.high_index = high_index
        self.low_index = high_index

        # initialize the reset value
        self.reset = 0

        # save the field access rights
        if hw not in hw_rights:
            print("field '%s' HW access right '%s' not in '%s'"%(name, hw, str(hw_rights)))
        if sw not in sw_rights:
            print("field '%s' SW access right '%s' not in '%s'"%(name, sw, str(sw_rights)))

        self.hw = hw
        self.sw = sw

    def add(self, index, reset, hw, sw):
        self.reset = self.reset | (reset << index)

        # check if the new index is the lowest
        if index < self.low_index:
            self.low_index = index

        if hw != self.hw:
            print("field '%s' all bits do not have same HW access rights (%s != %s)"%(self.name, self.hw, hw))
        if sw != self.sw:
            print("field '%s' all bits do not have same SW access rights (%s != %s)"%(self.name, self.sw, sw))


    def end(self, low_bitpos):
        # save the field bit position
        self.low_bitpos = low_bitpos
        # compute the field width
        self.width = self.high_bitpos - low_bitpos + 1
        # record the field type depending on the width
        if self.width <= 8:
            self.type = "uint8_t"
        elif self.width <= 16:
            self.type = "uint16_t"
        else:
            self.type = "uint32_t"

        # extract information
        self.mask = ((2**self.width) - 1) << self.low_bitpos

        if self.width != 1:
            if self.verbose:
                print("""  + +-> field : "%s"[%d..%d] at bit %d """%(self.name, self.high_index, self.low_index, self.low_bitpos))
        else:
            if self.verbose:
                print("""  + +-> field : "%s" at bit %d """%(self.name, self.low_bitpos))


class xls_register:
    "class of an xls register definition"
    def __init__(self, name, addr, msb, hw, sw, verbose=False):
        "constructor"

        # save the verbose directive
        self.verbose = verbose

        # check the name of the register
        result = re.compile("^\s*(\w+)\s*(\[\s*(\d+)\s*-\s*(\d+)\s*\])?\s*$").search(name)
        if result == None:
            raise LegalException("""ERROR: register "%s", name format is not parsable"""%(name, ))
        self.name = result.group(1)

        # check if there was an array defined
        if result.lastindex == 2:
            self.multi = True
            self.min = int(result.group(3))
            if self.min:
                raise LegalException("""ERROR: register array "%s", first index is not 0"""%(name, ))
            self.max = int(result.group(4))
        else:
            self.multi = False
            self.min = 0
            self.max = 0

        # check the format of the address:
        result = re.compile("^\s*(\+)?\s*([0-9a-fA-F]+)\s*(\'H)?").search(addr)
        if result == None:
            raise LegalException("""ERROR: register "%s" address "%s" not parsable"""%(name, addr))
        self.addr = int(result.group(2), 16)
        
        # check the format of the high bit
        try:
            self.msb = int(msb)
        except:
            raise LegalException("""ERROR: register "%s" can not parse MSB index "%s" """%(name, msb))
        
        if self.msb != 15 and self.msb != 31:
            raise LegalException("""ERROR: register "%s" MSB index "%d" not 15 or 31"""%(name, self.msb))

        self.width = self.msb + 1
        
        # build the type name
        self.type = "uint%d_t"%(self.width, )
        
        if self.verbose:
            print("  +-|-> register : %s %s"%(name, hex(self.addr)))

        # create an empty list of fields
        self.fields = []

        self.cur_field = None

        # save the register access rights
        if hw not in hw_rights:
            print("register '%s' HW access right '%s' not in '%s'"%(name, hw, str(hw_rights)))
        if sw not in sw_rights:
            print("register '%s' SW access right '%s' not in '%s'"%(name, sw, str(sw_rights)))

        self.hw = hw
        self.sw = sw

    def add_bit(self, name, bitpos, reset, hw, sw):
        # check if there is a bit at this position
        if name != '':
            # convert the reset value to an integer
            try:
                result = int(reset)
            except ValueError:
                result = 0
            reset = result

            # parse the field name
            result = re.compile("^\s*(\w+)\s*(\[(\d+)\]\s*)?$").search(name)
            if result == None:
                raise LegalException("""ERROR: bit name "%s" not parsable"""%(name, ))

            # check if it is a large field
            if result.lastindex == 2:
                # convert the index (it is impossible not to work)
                index = int(result.group(3), 0)
            else:
                index = 0

            # check if there is a current field being added
            if self.cur_field != None:
                # check that the name is identical to the current one
                if self.cur_field.name != result.group(1):
                    # finish the previous field
                    self.cur_field.end(bitpos + 1)
                    self.fields.append(self.cur_field)

                    # create a new field with the new name
                    self.cur_field = xls_field(result.group(1), bitpos, index, hw, sw, self.verbose)
                else:
                    # check that the index is following the previous one
                    if index != (self.cur_field.low_index - 1):
                        raise LegalException("""ERROR: index "%d" of field "%s" not in order with "%d" """%(index, name, self.cur_field.low_index))

                # add the bit
                self.cur_field.add(index, reset, hw, sw)

            else:
                # create a new field
                self.cur_field = xls_field(result.group(1), bitpos, index, hw, sw, self.verbose)

                # add the current  bit in the field
                self.cur_field.add(index, reset, hw, sw)

                # check if there is already a field with the same name
                if self.cur_field.name in map(lambda x: x.name, self.fields):
                    raise LegalException("""ERROR: field with same name "%s" already exists"""%(self.cur_field.name, ))

            # check if this is the last index or the last element in the register
            if (index == 0) or (bitpos == 0):
                self.cur_field.end(bitpos)
                self.fields.append(self.cur_field)
                self.cur_field = None

        else:
            # if there was a field in the process of entering, end it
            if self.cur_field != None:
                self.cur_field.end(bitpos + 1)
                self.fields.append(self.cur_field)
                self.cur_field = None


    def add_field(self, name, highbit, lowbit, reset, hw, sw):
        # create a new field with the new name
        new_field = xls_field(name, highbit, highbit-lowbit, hw, sw, self.verbose)

        # add all the bits to the field (in descending order)
        for bitpos in range(lowbit, highbit+1):
            new_field.add(bitpos-lowbit, 0, hw, sw)

        # finalize the field
        new_field.end(lowbit)

        # add the field to the list of fields
        self.fields.append(new_field)

        self.cur_field = None


    def end(self):
        # generate information
        #   - register mask
        self.mask = reduce(lambda x,y: x + y.mask, self.fields, 0)
        #  - invertedmask
# this does not work on all machines when mask = 0xFFFFFFFF so the workaround
#        self.invertedmask = (~self.mask)&0xFFFFFFFF
        if self.mask == 0xFFFFFFFF:
            self.invertedmask = 0
        else:
            temp = struct.pack("L", (~self.mask)&0xFFFFFFFF)
            self.invertedmask = struct.unpack("L", temp)[0]
        #   - reset value
        self.reset = reduce(lambda x,y: x + (y.reset<<y.low_bitpos), self.fields, 0)

        # check fields
        for field in self.fields:
            # check the access rights match between register and field
            if self.sw not in compat[field.sw]:
                print("register '%s' and field '%s' have incompatible types"%
                      (self.name, field.name))

            # provide information about field:
            #   - field is writable
            if field.sw in sw_writable:
                field.writable = True
                #   - if field is the only writable in register
                if len(filter(lambda x: x.sw in sw_writable, self.fields)):
                    field.unique_writable = True
                else:
                    field.unique_writable = False
            else:
                field.writable = False

        # provide information about register
        if self.sw in sw_writable:
            self.writable = True
            self.writable_fields = filter(lambda x: x.writable, self.fields)
        else:
            self.writable = False
            self.writable_fields = []

class xls_block:
    "class of an xls block definition"
    def __init__(self, name, addr, verbose):
        "constructor"

        # save the name of the block
        self.name = name
        self.verbose = verbose

        # check the format of the address:
        result = re.compile("^\s*\+\s*([0-9a-fA-F]{4})\.([0-9a-fA-F]{4})\s*\'H").search(addr)
        if result == None:
            raise LegalException("""ERROR: block address "%s" not parsable"""%(addr, ))

        self.addr = (int(result.group(1), 16) * 0x10000) + int(result.group(2), 16)

        if self.verbose:
            print(""" -|---> block : "%s" @ "%s" """%(name, hex(self.addr)))

        # create an empty list of registers
        self.registers = []

        # create set/clear dicos
        self.reg_clear = {}
        self.reg_set = {}

        # initialize the last noregister register index
        self.last_noregister_index = 0

    def start_register(self, name, addr, msb, hw, sw):
        # create new register
        self.cur_reg = xls_register(name, addr, msb, hw, sw, self.verbose)

    def end_register(self):
        # end of the register
        self.cur_reg.end()

        # check if there is already a register with the same address
        if self.cur_reg.addr in map(lambda x: x.addr, self.registers):
            raise LegalException("""ERROR: register with same address "%s" already exists"""%(hex(self.cur_reg.addr), ))

        # check if there is already a register with the same name (except noregister)
        if self.cur_reg.name != "NOREGISTER":
            if self.cur_reg.name in map(lambda x: x.name, self.registers):
                raise LegalException("""ERROR: register with same name "%s" already exists"""%(self.cur_reg.name, ))

            if self.cur_reg.name in map(lambda x: x.name, self.registers):
                raise LegalException("""ERROR: register with same name "%s" already exists"""%(self.cur_reg.name, ))

        # check if it is a noregister register
        if self.cur_reg.name == "NOREGISTER":
            self.cur_reg.name = self.cur_reg.name + str(self.last_noregister_index)
            self.last_noregister_index += 1
        self.registers.append(self.cur_reg)

        # multi register will be processed at next step


    def add_bit(self, name, bitpos, reset, hw, sw):
        self.cur_reg.add_bit(name, bitpos, reset, hw, sw)

    def add_field(self, name, highbit, lowbit, reset, hw, sw):
        self.cur_reg.add_field(name, highbit, lowbit, reset, hw, sw)

    def end(self):
        # search for the high register offset to compute the number of mapped words
        if len(self.registers) == 0:
            self.num_regs = 0
            self.decode_mask = 0
        else:
            # retrieve the highest offset address
            highest = max(map(lambda x:x.addr, self.registers))
            # turn in into a number of registers
            self.num_regs = (highest + 4)/4
            # build the decode mask
            self.decode_mask = 0
            for i in range(0,31):
                # check if the highest address is larger
                if (highest) < 2**i:
                    self.decode_mask = (2**i) - 1
                    break


class xls:
    "class of excel file"
    def __init__(self, xlsname, sheetname, verbose):
        "constructor"

        # save the verbose
        self.verbose = verbose
        # save the name of the excel file
        self.xlsname = xlsname
        # save the name of the excel sheet
        self.sheetname = sheetname

        # open the work book
        self.wbook = xlrd.open_workbook(self.xlsname)

        # check that the sheet exists
        try:
            self.wsheet = self.wbook.sheet_by_name(self.sheetname)
        except:
            raise LegalException("""Error: sheet "%s" does not exist in file "%s" """ % (self.sheetname, self.xlsname), 1)

        # create an empty list of blocks
        self.blocks = []

    def extract_all(self):
        "method to extract the information from the xls sheet"

        # initialize the state
        cur_row = 0

        # create a single block for the entire sheet (this is a trailing
        # part from another python script that had several blocks)
        self.cur_block = xls_block(self.sheetname, "+0000.0000'H", self.verbose)

        # save the block in the list of blocks (trailing from old script)
        self.blocks.append(self.cur_block)

        # loop on all the rows of the sheet
        while cur_row < self.wsheet.nrows:

            # if the current row column 0 is not empty
            if self.wsheet.cell_type(cur_row, 0) != xlrd.XL_CELL_EMPTY:
                # retrieve the column 0 value
                cur_value = self.wsheet.cell_value(cur_row, 0)

                if cur_value == "Address":
                    # create a new register with name and address
                    self.cur_block.start_register(self.wsheet.cell_value(cur_row, 3),
                                                  self.wsheet.cell_value(cur_row+2, 0),
                                                  self.wsheet.cell_value(cur_row+1, 3),
                                                  self.wsheet.cell_value(cur_row+2, 1),
                                                  self.wsheet.cell_value(cur_row+2, 2))

                    # pass the register line
                    cur_row += 1

                    # parse the bits of the register
                    cur_col = 3
                    while (self.wsheet.cell_value(cur_row, cur_col) != ""):
                        # check the bit number
                        try:
                            bitpos = int(self.wsheet.cell_value(cur_row, cur_col))
                        except:
                            print("""ERROR: cell "%s", unparsable bit value"""%
                                (xlrd.cellname(cur_row,cur_col), ))
                            bitpos = 0
                        # check if there is a correct bit number at this column
                        if bitpos == (self.cur_block.cur_reg.msb + 3 - cur_col):
                            # add the bit with the name, the bit position and reset value
                            self.cur_block.add_bit(self.wsheet.cell_value(cur_row + 1, cur_col),
                                                   bitpos,
                                                   self.wsheet.cell_value(cur_row + 2, cur_col),
                                                   self.wsheet.cell_value(cur_row + 4, cur_col),
                                                   self.wsheet.cell_value(cur_row + 5, cur_col))
                        else:
                            print("""ERROR: cell "%s", unexpected bit value "%s" """%
                                (xlrd.cellname(cur_row,cur_col), bitpos))
                        # increment the column index
                        cur_col += 1

                    # pass six rows
                    cur_row += 6

                    # finish the register
                    self.cur_block.end_register()
                else:
                    print("""ERROR: cell "%s", unexpected value "%s" """%(xlrd.cellname(cur_row,0), cur_value))

            cur_row += 1

        self.cur_block.end()

    def print_all(self):
        "method to print all the non empty cells of the sheet"
        for i in xrange(self.wsheet.nrows):
            for j in xrange(self.wsheet.ncols):
                if self.wsheet.cell_type(i, j) != xlrd.XL_CELL_EMPTY:
                    print("%s = %s"%(xlrd.cellname(i,j), self.wsheet.cell_value(i, j)))

