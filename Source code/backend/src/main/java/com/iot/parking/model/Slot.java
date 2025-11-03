package com.iot.parking.model;

import jakarta.persistence.*;

@Entity
@Table(name = "tblSlot")
public class Slot {
	@Id
	@GeneratedValue(strategy = GenerationType.AUTO)
	private Long id;
	private String slotId;
	private String state;
	
	public Slot() {}
	
	public Slot(String slotId, String state) {
		this.slotId = slotId;
		this.state = state;
	}

	public Long getId() {
		return id;
	}

	public void setId(Long id) {
		this.id = id;
	}

	public String getSlotId() {
		return slotId;
	}

	public void setSlotId(String slotId) {
		this.slotId = slotId;
	}

	public String getState() {
		return state;
	}

	public void setState(String state) {
		this.state = state;
	}
}
