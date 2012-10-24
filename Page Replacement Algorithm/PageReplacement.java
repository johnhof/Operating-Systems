/*John Hofrichter
 *OS Project 3
 *TA: Pj Dillion, Friday 1:00
 */
import java.io.*;
import java.util.*;

public class PageReplacement {
	static double maxframe;
	static double maxaddress;
	static double framenum;
	static PageTable pagetable;
	static ArrayList<Long> clock;
	static int clockpointer;

	// keep track of the data for processing
	static int accessnum;
	static int faultnum;
	static int writenum;

	static ArrayList<LinkedList<Long>> optlist;

	public static void main(String[] args) {
		String curline;
		int ERRORID = 0;
		int algorithm = -1;
		int NRUrefresh = 0;
		int NRUcounter = 0;
		Scanner stream = null;
		pagetable = null;
		// calculate the max address possible
		maxaddress = 2147483647;
		maxaddress = maxaddress * 2;
		maxframe = 1048576;// maximum 4kb pages possible
		optlist = new ArrayList<LinkedList<Long>>();
		clockpointer = 0;

		// check the argument length
		if (args.length < 5)
			ERRORID = 100;
		if (args.length > 7)
			ERRORID = 101;
		ERRORID = catchError(ERRORID);

		// retrieve the number of frames, store the frame size, and generate the
		// page table
		try {
			framenum = Long.parseLong(args[1]);
			if (framenum < 1)
				ERRORID = 1;
			if (framenum > maxframe)
				ERRORID = 1;
			pagetable = new PageTable(framenum);
		} catch (Exception e) {
			ERRORID = 1;
		}
		ERRORID = catchError(ERRORID);

		// retrieve the replacement algorithm
		if (args[3].compareToIgnoreCase("opt") == 0) {
			algorithm = 0;
			parseData(args[4]);
		} else if (args[3].compareToIgnoreCase("clock") == 0)
			algorithm = 1;
		else if (args[3].compareToIgnoreCase("nru") == 0)
			algorithm = 2;
		else
			ERRORID = 2;
		ERRORID = catchError(ERRORID);

		// if its the clock algorithm, create the circular list
		if (algorithm == 1) {
			clock = new ArrayList <Long>();
		}
		// if its an NRU algorithm, get the refresh rate
		else if (algorithm == 2) {
			try {
				NRUrefresh = (int) Long.parseLong(args[5]);
				if (NRUrefresh < 1)
					ERRORID = 3;
			} catch (Exception e) {
				ERRORID = 3;
			}
			ERRORID = catchError(ERRORID);
		}

		// retrieve the instruction file
		// keep in mind that NRU has preceding instructions in the line
		try {
			if (algorithm == 2)
				stream = new Scanner(new FileReader(new File(args[6])));
			else
				stream = new Scanner(new FileReader(new File(args[4])));
		} catch (Exception e) {
			ERRORID = 4;
		}
		ERRORID = catchError(ERRORID);

		while (stream.hasNext()) 
		{
			accessnum++;

			// the address should always be followed by a read/write command
			String straddress;
			Long address;
			Long page;
			String WR;
			int Dbit = 0;

			// retrieve and hold on to the String
			straddress = stream.next();
			//cut off the offset into each page table
			straddress = straddress.substring(0, 4);
			address = Long.parseLong(straddress, 16);

			// retrieve and hold on to the D bit
			if (!stream.hasNext())	break;
			WR = stream.next();
			if (WR.equals("W")) Dbit = 1;
			
			// calculate the page of the requested address
			page = new Long(frameCalc(straddress));

			// if the page is in the table, jut update the R and D bits
			if (pagetable.inTable(new Long(page))) 
			{
				// set the R bit
				pagetable.setRBit(page, 1);

				// if the instruction is a write, set its D bit
				if (WR.equals("W"))
					pagetable.setDBit(page, 1);
			}
			// Page fault, it must be handled by the appropriate algorithm
			else 
			{
				faultnum++;
				//p("\nPage Fault - ");
				// OPT
				if (algorithm == 0) 
				{
					optFault(page, Dbit, accessnum);
				}
				// Clock
				else if (algorithm == 1) 
				{
					clockFault(page, Dbit);
				}
				// NRU
				else if (algorithm == 2) 
				{
					nruFault(page, Dbit);

					// if NRU's instruction counter had reached the refresh
					// number, set all frames as unread
					if (NRUcounter > NRUrefresh) 
					{
						pagetable.NRUReset();
						NRUcounter = 0;
					}
				}
				NRUcounter++;
			}
		}
		p("\nNumber of frames:\t" + (int) framenum
				+ "\nTotal memory accesses:\t" + accessnum
				+ "\nTotal page faults:\t" + faultnum
				+ "\nTotal writes to disk:\t" + writenum);
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *OPT RELATED METHODS
	 *---------------------------------------------------------------------------------------------------------------------------*/

	// handle an opt algorithm fault
	private static void optFault(Long page, int Dbit, int fileindex) {
		Long farthestpage = null;//the page used farthest in the future
		Long furthestaccessindex = null; // the lineindex of the next page access page
		
		
		// if there is space, it is a compulsory miss
		if (pagetable.size() < framenum) 
		{
			pagetable.insertPage(page, Dbit);
			return;
		}

		//retrieve the page table entries, and set the first one as the frame to evict
		Iterator<Long> entries =  pagetable.entryList();
		Long frame = entries.next();
		
		//loop through each page, hold onto the page which will be referenced farthest in the future
		furthestaccessindex = new Long(fileindex);//set the current index to the frame access index
		for (farthestpage = frame; entries.hasNext(); frame = entries.next()) //set the farthest access to the first frame in the table
		{
			Long nextaccess = getNextAccess(frame, fileindex);
			
			//if this page is accessed in the future
			if(nextaccess != null)
			{
				//if it is accessed later than the stored index
				if(nextaccess.longValue()>furthestaccessindex.longValue())
				{
					furthestaccessindex = nextaccess;//mark the index as the new farthest access
					farthestpage = frame;//set the frame as the farthest page accesses
				}
			}
			//if it is not accessed, evict it
			else{
				furthestaccessindex = nextaccess;//mark the index as the new farthest access
				farthestpage = frame;//set the frame as the farthest page accesses			
				break;
			}
		}
		
		//evict the farthest page
		if(farthestpage != null)
		{
			if(pagetable.getDBit(farthestpage) == 1) writenum++;

			pagetable.removePage(farthestpage);
			pagetable.insertPage(page, Dbit);
		}
	}
	
	//retrieve the index of the next address access in the file
	public static Long getNextAccess(Long frame, int fileindex){
		Long index = null;
		Long nextaccess = null;
		LinkedList<Long> reflist = optlist.get((int)(frame.longValue()));
		
		//cycle through the list of frame accesses until we reach the next one to be referenced
		for(Long linenum: reflist){
			if((int)(linenum.longValue()) >= fileindex){
				nextaccess = linenum.longValue();
				break;
			}
		}
		return nextaccess;
	}

	// parse the input file into an arraylist of linkedlists to make opt
	// reasonable
	public static int parseData(String filename) {
		// populate the arraylist
		for (int i = 0; i < maxframe; i++) {
			LinkedList<Long> frameaccess = new LinkedList<Long>();
			optlist.add(frameaccess);
		}

		// retrieve the file
		Scanner stream = null;
		try {
			stream = new Scanner(new FileReader(new File(filename)));
			if (!stream.hasNext())
				return 0;
		} catch (Exception e) {
			catchError(4);
		}

		// build the arraylist
		int fileindex = 1;// keep track of the line in the file
		for (String straddress = stream.next(); stream.hasNext(); straddress = stream.next()) {

			// add the file line number to the list of address accesses
			optlist.get(frameCalc(straddress)).add(new Long(fileindex));

			// the next token is the read/write operator
			stream.next();
			if (!stream.hasNext())
				break;
			fileindex++;
		}
		return 1;
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *CLOCK RELATED METHODS
	 *---------------------------------------------------------------------------------------------------------------------------*/

	// handle a clock algorithm fault
	private static void clockFault(Long page, int Dbit) {

		// if there is space, it is a compulsory miss
		//add the page to the clock array
		if (pagetable.size() < framenum) 
		{
			pagetable.insertPage(page, Dbit);
			clock.add(page);

			//p("compulsory miss");
			return;
		}
		
		//cycle through the array
		for(int i = 0; i < framenum; i++)
		{
			Long currentpage = clock.get(i);
			if(!pagetable.inTable(currentpage)) catchError(102);
			
			//if this page has not been used
			if(pagetable.getRBit(currentpage) == 0)
			{
				//keep track of whether it was a write, and switch the page out
				if(pagetable.getDBit(currentpage) == 1)	writenum++;
				clock.set(i, page);
				pagetable.removePage(currentpage);
				pagetable.insertPage(page, Dbit);
				return;
			}
			//if the page has been referenced
			else pagetable.setRBit(currentpage, 0);
				
			//if we have reached the end of the array, cycle back through
			if(i == framenum) i = 0;
		}
		
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *NRU RELATED METHODS
	 *---------------------------------------------------------------------------------------------------------------------------*/

	// handle an nru algorithm fault
	private static void nruFault(Long page, int Dbit) {

		// if there is space, it is a compulsory miss
		if (pagetable.size() < framenum) {
			pagetable.insertPage(page, Dbit);
			return;
		}
		
		// we only need one of each priority for the sake of the algorithm
		Long p1 = null;// R = 0, D = 0 
		Long p2 = null;// R = 0, D = 1
		Long p3 = null;// R = 1, D = 0
		Long p4 = null;// R = 1, D = 1

		// cycle through the pages and put them into lists
		Iterator<Long> entries =  pagetable.entryList();
		for (Long frame = entries.next(); entries.hasNext(); frame = entries.next()) {

			// R = 0
			if (pagetable.getRBit(frame) == 0) {
				if (pagetable.getDBit(frame) == 0)
					p1 = frame;// R = 0, D = 0
				else
					p2 = frame; // R = 0, D = 1
			}
			// R = 1
			else {
				if (pagetable.getDBit(frame) == 0)
					p3 = frame;// R = 1, D = 0
				else
					p4 = frame; // R = 1, D = 1
			}
		}

		// if we found a p1 frame, replace it (R = 0, D = 0)
		if (p1 != null) {
			// remove the old frame and add our new one
			pagetable.removePage(p1);
			pagetable.insertPage(page, Dbit);
			return;
		}
		// if we found a p2 frame, replace it (R = 0, D = 1)
		if (p2 != null) {
			// remove the old frame and add our new one
			pagetable.removePage(p2);
			pagetable.insertPage(page, Dbit);
			writenum++;
			return;
		}
		// if we found a p23frame, replace it (R = 1, D = 0)
		if (p3 != null) {
			// remove the old frame and add our new one
			pagetable.removePage(p3);
			pagetable.insertPage(page, Dbit);
			return;
		}
		// if we found a p4 frame, replace it (R = 1, D = 1)
		if (p4 != null) {
			// remove the old frame and add our new one
			pagetable.removePage(p4);
			pagetable.insertPage(page, Dbit);
			writenum++;
			return;
		}
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *CALCULATE A FRAME, GIVEN A STRING
	 *---------------------------------------------------------------------------------------------------------------------------*/

	public static int frameCalc(String address) {
		//remove the offset to retrieve the frame number
		address = address.substring(0, 4);
		return (int) (Integer.parseInt(address, 16));
		/*
		// divide by the frame size to get the frame number
		double frame = (Long.parseLong(address, 16) / 4096);

		// compensate for loss of precision during division
		if (frame > framenum && frame - framenum < .00001)
			frame = framenum - 1;
		
		return (int) frame;
		*/
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *ERROR HANDLING
	 *---------------------------------------------------------------------------------------------------------------------------*/
	// exits if fatal error
	// return -1 if non-fatal error
	// return 0 if no error
	public static int catchError(int ID) {
		// Error id's 1-100
		// warning id's >100
		switch (ID) {
		// errors
		case 1:
			p("\nERROR: improperly formatted frame number "
					+ "\n[Frame number must be a positive Long (of size lass than 2,147,483,647)]");
			System.exit(0);
		case 2:
			p("\nERROR: improperly formatted algortihm type"
					+ "\n[Algorithm must be  opt, nru, or clock (not case sensitive)]");
			System.exit(0);
		case 3:
			p("\nERROR: improperly formatted NRU refresh rate"
					+ "\n[Refresh rate must be a positive Long]");
			System.exit(0);
		case 4:
			p("\nERROR: invalid instruction file");
			System.exit(0);

			// warnings
		case 100:
			p("\nWARNING: more arguements expected");
			return -1;
		case 101:
			p("\nWARNING: fewer arguements expected");
			return -1;
		case 102:
			p("\nWARNING: frame found in clock, but not in the pagetable");
			return -1;
		default:
			return 0;
		}
	}

	public static void p(String str) {
		System.out.print(str);
	}

	/*---------------------------------------------------------------------------------------------------------------------------
	 *PAGE TABLE CLASS
	 *---------------------------------------------------------------------------------------------------------------------------*/
	private static class PageTable {
		// remember the page size
		public double pagesize;

		// store the data in a hash table for quick access
		// holds only the R and D bits
		public static Hashtable<Long, int[]> entrytable;

		// {R,D}

		public PageTable(double psize) {
			pagesize = psize;
			entrytable = new Hashtable();
		}

		public static void main(String[] args) {
		}
		
		public static Iterator<Long> entryList(){
			return entrytable.keySet().iterator();
		}

		// store the R and D bits for the address at its hash location
		public void insertPage(Long page, int D) {
			int[] RDbit = { 1, D };
			entrytable.put(page, RDbit);
		}

		// return true if the page is in the table
		public boolean inTable(Long page) {
			if (entrytable.get(page) != null)
				return true;
			return false;
		}

		// retrieve the R bit from the frame hash location
		public int getRBit(Long page) {
			return entrytable.get(page)[0];
		}

		// retrieve the D bit from the frame hash location
		public int getDBit(Long page) {
			return entrytable.get(page)[1];
		}

		// set the R bit of the frame to 1
		public void setRBit(Long page, int value) {
			if (!inTable(page))
				return;

			// change only the R bit to 1
			int[] entry = entrytable.get(page);
			entry[0] = value;
			entrytable.put(page, entry);
		}

		// set the R bit of the frame to 1
		public void setDBit(Long page, int value) {
			if (!inTable(page))
				return;

			// change only the Dbit to one
			int[] entry = entrytable.get(page);
			entry[1] = value;
			entrytable.put(page, entry);
		}

		public void NRUReset() {
			Set<Long> pagelist = entrytable.keySet();
			Iterator<Long> list = pagelist.iterator();
			if(!list.hasNext()) return;
			for (Long page = list.next(); list.hasNext();page = list.next()) {
				//change the pages R bit to 0
				setRBit(page, 0);
			}
		}

		public int size() {
			return entrytable.size();
		}

		public void removePage(Long page) {
			entrytable.remove(page);
		}

		public double pageSize() {
			return pagesize;
		}
	}
}
